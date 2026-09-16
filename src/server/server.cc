#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

#include "trace.h"
#include "app_config.h"
#include "app_info.h"
#include "net/poll_table.h"
#include "net/socket.h"
#include "server/channel.h"
#include "server/crash_report.h"
#include "server/initial_request.h"
#include "server/server.h"

using namespace std;

using net::InetAddress;
using net::PollTable;
using net::Socket;

#ifndef POLLRDHUP
#define POLLRDHUP (0)
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct PendingConnection {
    uint64_t id;
    int fd;
    Clock::time_point deadline;
};

struct ChannelInfo {
    uint64_t id;
    string channel;
    shared_ptr<ConnectionQueue> queue;
};

enum CompletionType {
    CONNECTION_COMPLETED,
    CHANNEL_COMPLETED
};

struct Completion {
    CompletionType type;
    uint64_t id;
};

struct ChannelThreadInfo {
    const AppConfig *cfg;
    uint64_t id;
    string channel;
    shared_ptr<ConnectionQueue> queue;
};

Socket completion_socket;

void Notify(CompletionType type, uint64_t id) {
    Completion completion{};
    completion.type = type;
    completion.id = id;
    if (completion_socket.Send(&completion, sizeof completion) != sizeof completion)
        ERROR("Completion " << id << " could not notify the serving loop");
}

void NotifyConnection() {
    Notify(CONNECTION_COMPLETED, 0);
}

void *ChannelThread(void *argument) {
    ChannelThreadInfo *info = static_cast<ChannelThreadInfo *>(argument);
    const AppConfig *cfg = info->cfg;
    uint64_t id = info->id;
    string channel = info->channel;
    shared_ptr<ConnectionQueue> queue = info->queue;
    delete info;

    crash_report::SetChannel(id);
    RunChannel(*cfg, channel, queue, NotifyConnection);
    Notify(CHANNEL_COMPLETED, id);
    return NULL;
}

bool StartChannel(const pthread_attr_t *attributes, const AppConfig &cfg,
                  uint64_t id, const string &channel,
                  const shared_ptr<ConnectionQueue> &queue) {
    ChannelThreadInfo *info = new ChannelThreadInfo{&cfg, id, channel, queue};
    pthread_t thread;
    int error = pthread_create(&thread, attributes, ChannelThread, info);
    if (error == 0)
        return true;

    ERROR("A thread for channel " << channel << " can not be created: "
          << strerror(error));
    delete info;
    return false;
}

vector<PendingConnection>::iterator FindConnectionByDescriptor(
        vector<PendingConnection> &pending_connections, int fd) {
    return find_if(pending_connections.begin(), pending_connections.end(),
                   [fd](const PendingConnection &connection) {
                       return connection.fd == fd;
                   });
}

void ClosePendingConnection(PollTable &poll_table,
                            vector<PendingConnection> &pending_connections,
                            vector<PendingConnection>::iterator connection,
                            AppInfo &app_info,
                            const char *reason) {
    LOG("Closing connection [" << connection->fd << "] (" << reason << ")");
    shutdown(connection->fd, SHUT_RDWR);
    close(connection->fd);
    poll_table.Remove(connection->fd);
    pending_connections.erase(connection);
    app_info->num_connections--;
}

int GetPollTimeout(const vector<PendingConnection> &pending_connections) {
    Clock::time_point deadline;
    bool found = false;
    for (const PendingConnection &connection : pending_connections) {
        if (!found || connection.deadline < deadline) {
            deadline = connection.deadline;
            found = true;
        }
    }
    if (!found)
        return -1;

    Clock::duration remaining = deadline - Clock::now();
    if (remaining <= Clock::duration::zero())
        return 0;

    std::chrono::milliseconds milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
    if (milliseconds.count() >= INT_MAX)
        return INT_MAX;
    return static_cast<int>(milliseconds.count()) + 1;
}

void ExpirePendingConnections(PollTable &poll_table,
                              vector<PendingConnection> &pending_connections,
                              AppInfo &app_info) {
    Clock::time_point now = Clock::now();
    for (size_t i = 0; i < pending_connections.size();) {
        if (pending_connections[i].deadline <= now) {
            ClosePendingConnection(poll_table, pending_connections,
                                   pending_connections.begin() + i, app_info,
                                   "identification time-out");
        } else {
            ++i;
        }
    }
}

bool DispatchConnection(const AppConfig &cfg, const pthread_attr_t *attributes,
                        vector<ChannelInfo> &channels,
                        const PendingConnection &connection,
                        const InitialRequest &request) {
    if (request.new_channel) {
        channels.erase(remove_if(channels.begin(), channels.end(),
                                 [](ChannelInfo &entry) {
                                     return entry.queue->IsClosed();
                                 }), channels.end());
        if (channels.size() >= static_cast<size_t>(cfg.max_connections())) {
            LOG("A new channel was refused because the limit has been reached");
            return false;
        }

        string channel = to_string(connection.id);
        shared_ptr<ConnectionQueue> queue = make_shared<ConnectionQueue>();
        if (!queue->IsValid()) {
            ERROR("The connection queue can not be created");
        } else if (!queue->Push({connection.fd})) {
            ERROR("The initial channel connection can not be queued");
        } else if (StartChannel(attributes, cfg, connection.id, channel, queue)) {
            channels.push_back({connection.id, channel, queue});
            LOG("Creating channel " << channel << " for connection ["
                                    << connection.id << "]");
            return true;
        }
        return false;
    }

    vector<ChannelInfo>::iterator channel =
            find_if(channels.begin(), channels.end(),
                    [&request](const ChannelInfo &entry) {
                        return entry.channel == request.channel;
                    });
    if (channel == channels.end()) {
        LOG("The connection [" << connection.id << "] references unknown channel "
                               << request.channel);
        return false;
    }
    if (channel->queue->Push({connection.fd}))
        return true;
    if (channel->queue->IsClosed())
        channels.erase(channel);
    return false;
}

}

int RunServer(const AppConfig &cfg, AppInfo &app_info,
              Socket &listen_socket, int supervisor_fd,
              const string &log_name, const string &description,
              const string &restart_message) {
    if (!crash_report::Initialize(supervisor_fd)) {
        cerr << "Crash reporting can not be initialized: " << strerror(errno) << endl;
        return SERVER_STARTUP_FAILURE;
    }
    if (!trace::Initialize(log_name)) {
        cerr << "The logging system can not be initialized" << endl;
        return SERVER_STARTUP_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);
    app_info->num_connections = 0;
    if (!restart_message.empty())
        LOG(restart_message);
    LOG(description << " started");
    LOG("Serving process created (PID = " << getpid() << ")");

    int completion_fds[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, completion_fds) != 0) {
        ERROR("The completion socket can not be created: " << strerror(errno));
        trace::Drain();
        return SERVER_STARTUP_FAILURE;
    }
    Socket completion_reader(completion_fds[0]);
    completion_socket = completion_fds[1];

    pthread_attr_t attributes;
    int thread_error = pthread_attr_init(&attributes);
    if (thread_error != 0) {
        ERROR("The channel thread attributes can not be initialized: "
              << strerror(thread_error));
        trace::Drain();
        return SERVER_STARTUP_FAILURE;
    }
    thread_error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (thread_error != 0) {
        ERROR("Detached channel threads can not be configured: "
              << strerror(thread_error));
        pthread_attr_destroy(&attributes);
        trace::Drain();
        return SERVER_STARTUP_FAILURE;
    }

    PollTable poll_table;
    poll_table.Add(listen_socket, POLLIN);
    poll_table.Add(supervisor_fd, POLLIN);
    poll_table.Add(completion_reader, POLLIN);
    poll_table.Add(trace::ReadDescriptor(), POLLIN);

    vector<PendingConnection> pending_connections;
    vector<ChannelInfo> channels;
    uint64_t next_connection_id = 0;
    int result = 0;

    for (;;) {
        int ready = poll_table.Poll(GetPollTimeout(pending_connections));
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            ERROR("The serving poll failed: " << strerror(errno));
            result = -1;
            break;
        }

        if (poll_table[1].revents) {
            // The supervisor never writes to this socket. Any event means its
            // endpoint was closed, so the serving process must exit.
            break;
        }

        bool connection_ready = false;
        bool log_ready = ready > 0 && (poll_table[3].revents & POLLIN);
        if (ready > 0) {
            connection_ready = poll_table[0].revents || poll_table[2].revents;
            for (int i = 4; i < poll_table.GetSize(); ++i)
                connection_ready = connection_ready || poll_table[i].revents;

            if (poll_table[0].revents & POLLIN) {
                InetAddress from_address;
                Socket socket = listen_socket.Accept(&from_address);
                if (socket == -1) {
                    ERROR("Error accepting a new connection: " << strerror(errno));
                } else if (app_info->num_connections >= cfg.max_connections()) {
                    LOG("Connection refused because the limit has been reached");
                    socket.Close();
                } else {
                    uint64_t id = next_connection_id++;
                    int fd = socket;
                    LOG("New connection from " << from_address.GetPath() << ":"
                                                << from_address.GetPort() << " ["
                                                << fd << ":" << id << "]");
                    poll_table.Add(fd, POLLIN | POLLRDHUP);
                    pending_connections.push_back({id, fd,
                                                   Clock::now() +
                                                           std::chrono::seconds(
                                                                   cfg.initial_timeout())});
                    app_info->num_connections++;
                }
            }

            if (poll_table[2].revents & POLLIN) {
                Completion completion;
                if (completion_reader.Receive(&completion, sizeof completion) ==
                    sizeof completion) {
                    if (completion.type == CONNECTION_COMPLETED) {
                        if (app_info->num_connections > 0)
                            app_info->num_connections--;
                    } else if (completion.type == CHANNEL_COMPLETED) {
                        vector<ChannelInfo>::iterator channel =
                                find_if(channels.begin(), channels.end(),
                                        [&completion](const ChannelInfo &entry) {
                                            return entry.id == completion.id;
                                        });
                        if (channel != channels.end()) {
                            LOG("The channel " << channel->channel << " has ended");
                            channels.erase(channel);
                        }
                    }
                } else {
                    ERROR("Could not receive a completion record");
                }
            }

            for (int i = 4; i < poll_table.GetSize();) {
                short events = poll_table[i].revents;
                if (!events) {
                    ++i;
                    continue;
                }

                int fd = poll_table[i].fd;
                vector<PendingConnection>::iterator connection =
                        FindConnectionByDescriptor(pending_connections, fd);
                if (connection == pending_connections.end()) {
                    close(fd);
                    poll_table.RemoveAt(i);
                    continue;
                }

                uint64_t id = connection->id;
                if (events & POLLIN) {
                    InitialRequest request = InspectInitialRequest(fd);
                    if (request.state == REQUEST_REJECTED) {
                        ClosePendingConnection(poll_table, pending_connections,
                                               connection, app_info,
                                               "not a JPIP client");
                        continue;
                    }
                    if (request.state == REQUEST_ACCEPTED) {
                        if (!DispatchConnection(cfg, &attributes, channels,
                                                *connection, request)) {
                            ClosePendingConnection(poll_table,
                                                   pending_connections,
                                                   connection, app_info,
                                                   "dispatch failed");
                            continue;
                        }
                        poll_table.Remove(fd);
                        pending_connections.erase(connection);
                        LOG("JPIP connection identified [" << fd << ":" << id << "]");
                        continue;
                    }
                }
                if (events & (POLLRDHUP | POLLERR | POLLHUP | POLLNVAL)) {
                    ClosePendingConnection(poll_table, pending_connections,
                                           connection, app_info,
                                           "socket closed");
                    continue;
                }
                ++i;
            }
        }

        ExpirePendingConnections(poll_table, pending_connections, app_info);
        if (log_ready && !connection_ready)
            trace::DrainOne();
    }

    pthread_attr_destroy(&attributes);
    trace::Drain();
    return result;
}
