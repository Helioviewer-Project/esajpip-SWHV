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
#include "config.h"
#include "net/address.h"
#include "net/poll_table.h"
#include "server/channel.h"
#include "server/initial_request.h"
#include "server/server.h"

using namespace std;

using net::InetAddress;
using net::PollTable;

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
    const AppConfig *cfg;
    uint64_t id;
    shared_ptr<ConnectionQueue> queue;
};

enum PollSlot {
    LISTEN,
    SUPERVISOR,
    COMPLETION,
    LOG,
    FIRST_PENDING
};

enum CompletionType {
    CONNECTION_COMPLETED,
    CHANNEL_COMPLETED
};

struct Completion {
    CompletionType type;
    uint64_t id;
};

int completion_socket = -1;

void Notify(CompletionType type, uint64_t id) {
    Completion completion{};
    completion.type = type;
    completion.id = id;
    ssize_t sent;
    do {
        sent = send(completion_socket, &completion, sizeof completion, 0);
    } while (sent < 0 && errno == EINTR);
    if (sent != sizeof completion)
        ERROR("Completion " << id << " could not notify the serving loop");
}

void NotifyConnection() {
    Notify(CONNECTION_COMPLETED, 0);
}

void *ChannelThread(void *argument) {
    ChannelInfo *info = static_cast<ChannelInfo *>(argument);
    const AppConfig *cfg = info->cfg;
    uint64_t id = info->id;
    shared_ptr<ConnectionQueue> queue = info->queue;
    delete info;

    RunChannel(*cfg, to_string(id), queue, NotifyConnection);
    Notify(CHANNEL_COMPLETED, id);
    return NULL;
}

bool StartChannel(const pthread_attr_t *attributes, const ChannelInfo &channel) {
    ChannelInfo *info = new ChannelInfo(channel);
    pthread_t thread;
    int error = pthread_create(&thread, attributes, ChannelThread, info);
    if (error == 0)
        return true;

    ERROR("A thread for channel " << channel.id << " can not be created: "
          << strerror(error));
    delete info;
    return false;
}

void ClosePendingConnection(PollTable &poll_table,
                            vector<PendingConnection> &pending_connections,
                            size_t index,
                            int &num_connections,
                            const char *reason) {
    int fd = pending_connections[index].fd;
    LOG("Closing connection [" << fd << "] (" << reason << ")");
    shutdown(fd, SHUT_RDWR);
    close(fd);
    poll_table.RemoveAt(FIRST_PENDING + static_cast<int>(index));
    pending_connections.erase(pending_connections.begin() + index);
    num_connections--;
}

int GetPollTimeout(const vector<PendingConnection> &pending_connections) {
    if (pending_connections.empty())
        return -1;

    Clock::duration remaining =
            pending_connections.front().deadline - Clock::now();
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
                              int &num_connections) {
    Clock::time_point now = Clock::now();
    while (!pending_connections.empty() &&
           pending_connections.front().deadline <= now)
        ClosePendingConnection(poll_table, pending_connections, 0,
                               num_connections, "identification time-out");
}

bool DispatchConnection(const AppConfig &cfg, const pthread_attr_t *attributes,
                        vector<ChannelInfo> &channels,
                        const PendingConnection &connection,
                        const InitialRequest &request) {
    if (request.new_channel) {
        channels.erase(remove_if(channels.begin(), channels.end(),
                                 [](const ChannelInfo &entry) {
                                     return entry.queue->IsClosed();
                                 }), channels.end());
        if (channels.size() >= static_cast<size_t>(cfg.max_connections())) {
            LOG("A new channel was refused because the limit has been reached");
            return false;
        }

        shared_ptr<ConnectionQueue> queue = make_shared<ConnectionQueue>();
        ChannelInfo channel = {&cfg, connection.id, queue};
        if (!queue->IsValid()) {
            ERROR("The connection queue can not be created");
        } else if (!queue->Push(connection.fd)) {
            ERROR("The initial channel connection can not be queued");
        } else if (StartChannel(attributes, channel)) {
            channels.push_back(channel);
            LOG("Creating channel " << channel.id << " for connection ["
                                    << connection.id << "]");
            return true;
        }
        return false;
    }

    vector<ChannelInfo>::iterator channel =
            find_if(channels.begin(), channels.end(),
                    [&request](const ChannelInfo &entry) {
                        return entry.id == request.channel;
                    });
    if (channel == channels.end()) {
        LOG("The connection [" << connection.id << "] references unknown channel "
                               << request.channel);
        return false;
    }
    if (channel->queue->Push(connection.fd))
        return true;
    if (channel->queue->IsClosed())
        channels.erase(channel);
    return false;
}

}

int RunServer(const AppConfig &cfg, int listen_socket, int supervisor_fd,
              const string &log_name, const string &description,
              const string &restart_message) {
    if (!trace::Initialize(log_name)) {
        cerr << "The logging system can not be initialized" << endl;
        return SERVER_STARTUP_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);
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
    int completion_reader = completion_fds[0];
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

    // Pending records remain aligned with poll slots starting at FIRST_PENDING.
    // Both sequences preserve acceptance and deadline order when entries move
    // to a channel or close.
    vector<PendingConnection> pending_connections;
    vector<ChannelInfo> channels;
    uint64_t next_connection_id = 0;
    int num_connections = 0;
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

        if (poll_table[SUPERVISOR].revents) {
            // The supervisor never writes to this socket. Any event means its
            // endpoint was closed, so the serving process must exit.
            break;
        }

        bool connection_ready = false;
        bool log_ready = ready > 0 && (poll_table[LOG].revents & POLLIN);
        if (ready > 0) {
            connection_ready = poll_table[LISTEN].revents ||
                    poll_table[COMPLETION].revents;
            for (int i = FIRST_PENDING; i < poll_table.GetSize(); ++i)
                connection_ready = connection_ready || poll_table[i].revents;

            if (poll_table[LISTEN].revents & POLLIN) {
                InetAddress from_address;
                socklen_t from_size = from_address.GetSize();
                int fd = accept(listen_socket, from_address.GetSockAddr(),
                                &from_size);
                if (fd < 0) {
                    ERROR("Error accepting a new connection: " << strerror(errno));
                } else if (num_connections >= cfg.max_connections()) {
                    LOG("Connection refused because the limit has been reached");
                    close(fd);
                } else {
                    uint64_t id = next_connection_id++;
                    LOG("New connection from " << from_address.GetPath() << ":"
                                                << from_address.GetPort() << " ["
                                                << fd << ":" << id << "]");
                    poll_table.Add(fd, POLLIN | POLLRDHUP);
                    pending_connections.push_back({id, fd,
                                                   Clock::now() +
                                                           std::chrono::seconds(
                                                                   cfg.initial_timeout())});
                    num_connections++;
                }
            }

            if (poll_table[COMPLETION].revents & POLLIN) {
                Completion completion;
                if (recv(completion_reader, &completion, sizeof completion, 0) ==
                    sizeof completion) {
                    if (completion.type == CONNECTION_COMPLETED) {
                        if (num_connections > 0)
                            num_connections--;
                    } else if (completion.type == CHANNEL_COMPLETED) {
                        vector<ChannelInfo>::iterator channel =
                                find_if(channels.begin(), channels.end(),
                                        [&completion](const ChannelInfo &entry) {
                                            return entry.id == completion.id;
                                        });
                        if (channel != channels.end()) {
                            LOG("The channel " << channel->id << " has ended");
                            channels.erase(channel);
                        }
                    }
                } else {
                    ERROR("Could not receive a completion record");
                }
            }

            for (int i = FIRST_PENDING; i < poll_table.GetSize();) {
                short events = poll_table[i].revents;
                if (!events) {
                    ++i;
                    continue;
                }

                size_t pending_index = static_cast<size_t>(i - FIRST_PENDING);
                PendingConnection &connection =
                        pending_connections[pending_index];
                int fd = connection.fd;
                uint64_t id = connection.id;
                if (events & POLLIN) {
                    InitialRequest request = InspectInitialRequest(fd);
                    if (request.state == REQUEST_REJECTED) {
                        ClosePendingConnection(poll_table, pending_connections,
                                               pending_index, num_connections,
                                               "not a JPIP client");
                        continue;
                    }
                    if (request.state == REQUEST_ACCEPTED) {
                        if (!DispatchConnection(cfg, &attributes, channels,
                                                connection, request)) {
                            ClosePendingConnection(poll_table,
                                                   pending_connections,
                                                   pending_index,
                                                   num_connections,
                                                   "dispatch failed");
                            continue;
                        }
                        poll_table.RemoveAt(i);
                        pending_connections.erase(
                                pending_connections.begin() + pending_index);
                        LOG("JPIP connection identified [" << fd << ":" << id << "]");
                        continue;
                    }
                }
                if (events & (POLLRDHUP | POLLERR | POLLHUP | POLLNVAL)) {
                    ClosePendingConnection(poll_table, pending_connections,
                                           pending_index, num_connections,
                                           "socket closed");
                    continue;
                }
                ++i;
            }
        }

        ExpirePendingConnections(poll_table, pending_connections,
                                 num_connections);
        if (log_ready && !connection_ready)
            trace::DrainOne();
    }

    pthread_attr_destroy(&attributes);
    if (result == 0)
        LOG("Serving process stopping");
    trace::Drain();
    return result;
}
