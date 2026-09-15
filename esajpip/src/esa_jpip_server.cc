#ifdef _PLATFORM_LINUX
#include <sys/prctl.h>
#endif

#include <sys/socket.h>
#include <sys/wait.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "trace.h"
#include "app_info.h"
#include "app_config.h"
#include "args_parser.h"
#include "net/poll_table.h"
#include "net/socket.h"
#include "server/channel.h"
#include "server/connection_admission.h"

using namespace std;
using namespace net;

#define SERVER_VERSION    "2.0-rc1"
#define SERVER_NAME       "ESA JPIP Server"
#define SERVER_APP_NAME   "esa_jpip_server"
#define CONFIG_FILE       "server.ini"

#ifndef POLLRDHUP
#define POLLRDHUP         (0)
#endif

using Clock = chrono::steady_clock;

struct Connection {
    uint64_t id;
    int fd;
    bool pending;
    Clock::time_point deadline;
};

struct ChannelInfo {
    uint64_t id;
    string channel;
    shared_ptr<ChannelInbox> inbox;
};

static AppConfig cfg;
static AppInfo app_info;
static Socket child_socket;
static PollTable poll_table;
static vector<Connection> connections;
static uint64_t next_connection_id = 0;
static volatile sig_atomic_t child_lost = 0;
static UnixAddress child_address("/tmp/child_unix_address");
static UnixAddress father_address("/tmp/father_unix_address");
static UnixAddress channel_address("/tmp/channel_unix_address");

static int ChildProcess(const pthread_attr_t *pattr);
static bool StartChannel(const pthread_attr_t *pattr, uint64_t id, const string &channel,
                         const shared_ptr<ChannelInbox> &inbox);
static void NotifyParent(uint64_t id);

static void *ChannelThread(void *arg);

static void SIGCHLD_handler(int signal) {
    wait(NULL);
    child_lost = 1;
}

static vector<Connection>::iterator FindConnection(uint64_t id) {
    return find_if(connections.begin(), connections.end(), [id](const Connection &connection) {
        return connection.id == id;
    });
}

static vector<Connection>::iterator FindConnectionByDescriptor(int fd) {
    return find_if(connections.begin(), connections.end(), [fd](const Connection &connection) {
        return connection.fd == fd;
    });
}

static void CloseConnection(uint64_t id, const char *reason) {
    vector<Connection>::iterator connection = FindConnection(id);
    if (connection == connections.end())
        return;

    int fd = connection->fd;
    LOG("Closing connection [" << fd << "] (" << reason << ")");
    close(fd);
    poll_table.Remove(fd);
    connections.erase(connection);
    app_info->num_connections = static_cast<int>(connections.size());
}

static int GetPollTimeout() {
    Clock::time_point deadline;
    bool found = false;
    for (const Connection &connection : connections) {
        if (connection.pending && (!found || connection.deadline < deadline)) {
            deadline = connection.deadline;
            found = true;
        }
    }
    if (!found)
        return -1;

    Clock::duration remaining = deadline - Clock::now();
    if (remaining <= Clock::duration::zero())
        return 0;

    chrono::milliseconds milliseconds = chrono::duration_cast<chrono::milliseconds>(remaining);
    if (milliseconds.count() >= INT_MAX)
        return INT_MAX;
    return static_cast<int>(milliseconds.count()) + 1;
}

static void ExpirePendingConnections() {
    Clock::time_point now = Clock::now();
    for (size_t i = 0; i < connections.size();) {
        if (connections[i].pending && connections[i].deadline <= now) {
            uint64_t id = connections[i].id;
            CloseConnection(id, "identification time-out");
        } else {
            ++i;
        }
    }
}

int main(int argc, char **argv) {
    if (!app_info.Init())
        return CERR("The shared information can not be set");
    if (!cfg.Load(CONFIG_FILE))
        return CERR("The configuration file '" << CONFIG_FILE << "' can not be read");
    if (!ParseArgs(app_info, argc, argv))
        return -1;
    if (app_info.is_running())
        return CERR("The server is already running");

    app_info->father_pid = getpid();

    cout << endl << SERVER_NAME << " " << SERVER_VERSION << endl;
    cout << endl << '-' << cfg << endl;

    if (cfg.file_logging())
        TraceSystem::AppendToFile(cfg.log_directory() + SERVER_APP_NAME);

    Socket listen_socket;
    InetAddress listen_addr = cfg.address().empty()
                                  ? InetAddress(cfg.port())
                                  : InetAddress(cfg.address().c_str(), cfg.port());
    if (!listen_socket.OpenInet())
        return CERR("The server listen socket can not be created");
    if (!listen_socket.ListenAt(listen_addr))
        return CERR("The server listen socket can not be initialized");

    LOG(SERVER_NAME << " " << SERVER_VERSION << " started");

    signal(SIGCHLD, SIG_IGN);

    poll_table.Add(listen_socket, POLLIN);

    Socket father_socket;
    if (!father_socket.OpenUnix(SOCK_DGRAM)) {
        ERROR("The father unix socket can not be created");
        return -1;
    }
    if (!father_socket.BindTo(father_address.Reset())) {
        ERROR("The father unix socket can not be bound");
        return -1;
    }

    poll_table.Add(father_socket, POLLIN);

    pthread_attr_t pattr;
    int pthread_error = pthread_attr_init(&pattr);
    if (pthread_error != 0)
        return CERR("The channel thread attributes can not be initialized: " << strerror(pthread_error));
    pthread_error = pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);
    if (pthread_error != 0) {
        pthread_attr_destroy(&pattr);
        return CERR("Detached channel threads can not be configured: " << strerror(pthread_error));
    }

father_begin:

    if (!fork())
        return ChildProcess(&pattr);

    // in father
    signal(SIGCHLD, SIGCHLD_handler);

    for (;;) {
        int res = poll_table.Poll(GetPollTimeout());

        if (child_lost) {
            child_lost = 0;
            goto father_begin;
        }

        if (res < 0 && errno != EINTR)
            ERROR("Connection poll failed: " << strerror(errno));

        if (res > 0) {
            if (poll_table[0].revents & POLLIN) {
                InetAddress from_addr;
                Socket new_conn = listen_socket.Accept(&from_addr);

                if (new_conn == -1) {
                    ERROR("Error accepting a new connection: " << strerror(errno));
                } else if (connections.size() >= static_cast<size_t>(cfg.max_connections())) {
                    LOG("Connection refused because the limit has been reached");
                    new_conn.Close();
                } else {
                    uint64_t id = next_connection_id++;
                    LOG("New connection from " << from_addr.GetPath() << ":" << from_addr.GetPort()
                                                 << " [" << static_cast<int>(new_conn) << ":" << id << "]");
                    poll_table.Add(new_conn, POLLIN | POLLRDHUP | POLLERR | POLLHUP | POLLNVAL);
                    connections.push_back({id, new_conn, true,
                                           Clock::now() + chrono::seconds(cfg.admission_timeout())});
                    app_info->num_connections = static_cast<int>(connections.size());
                }
            }

            if (poll_table[1].revents & POLLIN) {
                uint64_t id;
                if (father_socket.Receive(&id, sizeof id) == sizeof id)
                    CloseConnection(id, "client finished");
                else
                    ERROR("Could not receive connection identifier");
            }

            for (int i = 2; i < poll_table.GetSize();) {
                short events = poll_table[i].revents;
                if (!events) {
                    i++;
                    continue;
                }

                int fd = poll_table[i].fd;
                vector<Connection>::iterator connection = FindConnectionByDescriptor(fd);
                if (connection == connections.end()) {
                    close(fd);
                    poll_table.RemoveAt(i);
                    continue;
                }

                uint64_t id = connection->id;
                if (connection->pending && (events & POLLIN)) {
                    Admission admission = CheckAdmission(fd);
                    if (admission.result == ADMISSION_REJECTED) {
                        CloseConnection(id, "not a JPIP client");
                        continue;
                    }
                    if (admission.result == ADMISSION_ACCEPTED) {
                        if (!father_socket.SendDescriptor(child_address, fd, id)) {
                            ERROR("The admitted socket can not be sent to the child process: " << strerror(errno));
                            CloseConnection(id, "dispatch failed");
                            continue;
                        }
                        connection->pending = false;
                        poll_table[i].events = POLLRDHUP | POLLERR | POLLHUP | POLLNVAL;
                        LOG("JPIP connection admitted [" << fd << ":" << id << "]");
                    }
                }
                if (events & (POLLRDHUP | POLLERR | POLLHUP | POLLNVAL)) {
                    CloseConnection(id, "socket closed");
                    continue;
                }
                i++;
            }
        }

        ExpirePendingConnections();
    }

    pthread_attr_destroy(&pattr);

    listen_socket.Close();
    return 0;
}

static int ChildProcess(const pthread_attr_t *pattr) {
    app_info->child_iterations++;
    app_info->child_pid = getpid();

    signal(SIGPIPE, SIG_IGN);

#ifdef _PLATFORM_LINUX
    prctl(PR_SET_PDEATHSIG, SIGHUP);
#endif

    LOG("Child process created (PID = " << getpid() << ")");

    if (!child_socket.OpenUnix(SOCK_DGRAM)) {
        ERROR("The child unix socket can not be created");
        return -1;
    }
    if (!child_socket.BindTo(child_address.Reset())) {
        ERROR("The child unix socket can not be bound");
        return -1;
    }

    Socket channel_socket;
    if (!channel_socket.OpenUnix(SOCK_DGRAM)) {
        ERROR("The channel notification socket can not be created");
        return -1;
    }
    if (!channel_socket.BindTo(channel_address.Reset())) {
        ERROR("The channel notification socket can not be bound");
        return -1;
    }

    for (const Connection &connection : connections) {
        if (!connection.pending)
            shutdown(connection.fd, SHUT_RDWR);
        close(connection.fd);
    }
    connections.clear();

    vector<ChannelInfo> channels;
    for (;;) {
        pollfd fds[] = {
            {child_socket, POLLIN, 0},
            {channel_socket, POLLIN, 0}
        };
        int result;
        do {
            result = poll(fds, 2, -1);
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            ERROR("The child dispatcher poll failed: " << strerror(errno));
            return -1;
        }

        if (fds[1].revents & POLLIN) {
            uint64_t id;
            if (channel_socket.Receive(&id, sizeof id) == sizeof id) {
                vector<ChannelInfo>::iterator channel =
                        find_if(channels.begin(), channels.end(), [id](const ChannelInfo &route) {
                            return route.id == id;
                        });
                if (channel != channels.end()) {
                    LOG("The channel " << channel->channel << " has ended");
                    channels.erase(channel);
                }
            } else {
                ERROR("Could not receive the completed channel identifier");
            }
        }

        if (!(fds[0].revents & POLLIN))
            continue;

        int fd;
        uint64_t connection_id;
        if (!child_socket.ReceiveDescriptor(&fd, &connection_id)) {
            ERROR("The admitted socket can not be received by the child process: " << strerror(errno));
            continue;
        }

        Admission admission = CheckAdmission(fd);
        if (admission.result != ADMISSION_ACCEPTED) {
            LOG("The admitted connection [" << connection_id << "] has no routable request");
        } else if (admission.new_channel) {
            channels.erase(remove_if(channels.begin(), channels.end(), [](ChannelInfo &route) {
                return route.inbox->IsClosed();
            }), channels.end());
            if (channels.size() >= static_cast<size_t>(cfg.max_connections())) {
                LOG("A new channel was refused because the limit has been reached");
            } else {
                string channel = to_string(connection_id);
                shared_ptr<ChannelInbox> inbox = make_shared<ChannelInbox>();
                if (!inbox->IsValid()) {
                    ERROR("The channel inbox can not be created");
                } else if (!inbox->Push({connection_id, fd})) {
                    ERROR("The initial channel connection can not be queued");
                } else if (StartChannel(pattr, connection_id, channel, inbox)) {
                    channels.push_back({connection_id, channel, inbox});
                    LOG("Creating channel " << channel << " for connection [" << connection_id << "]");
                    continue;
                }
            }
        } else {
            vector<ChannelInfo>::iterator channel =
                    find_if(channels.begin(), channels.end(), [&admission](const ChannelInfo &route) {
                        return route.channel == admission.channel;
                    });
            if (channel == channels.end()) {
                LOG("The connection [" << connection_id << "] references unknown channel "
                    << admission.channel);
            } else if (channel->inbox->Push({connection_id, fd})) {
                continue;
            } else if (channel->inbox->IsClosed()) {
                channels.erase(channel);
            }
        }

        shutdown(fd, SHUT_RDWR);
        close(fd);
        NotifyParent(connection_id);
    }

    return 0;
}

static void NotifyParent(uint64_t id) {
    if (child_socket.SendTo(father_address, &id, sizeof id) == sizeof id)
        return;
    ERROR("The completed connection [" << id << "] could not notify the parent");
}

static bool StartChannel(const pthread_attr_t *pattr, uint64_t id, const string &channel,
                         const shared_ptr<ChannelInbox> &inbox) {
    ChannelInfo *channel_info = new ChannelInfo{id, channel, inbox};
    pthread_t service_tid;
    int pthread_error = pthread_create(&service_tid, pattr, ChannelThread, channel_info);
    if (pthread_error == 0)
        return true;

    ERROR("A thread for channel " << channel << " can not be created: "
          << strerror(pthread_error));
    delete channel_info;
    return false;
}

static void *ChannelThread(void *arg) {
    ChannelInfo *channel_info = static_cast<ChannelInfo *>(arg);
    uint64_t id = channel_info->id;
    string channel = channel_info->channel;
    shared_ptr<ChannelInbox> inbox = channel_info->inbox;
    delete channel_info;

    RunChannel(cfg, channel, inbox, NotifyParent);

    Socket socket;
    if (socket.OpenUnix(SOCK_DGRAM)) {
        if (socket.SendTo(channel_address, &id, sizeof id) != sizeof id)
            ERROR("The completed channel " << channel << " could not notify the dispatcher");
        socket.Close();
    } else {
        ERROR("A notification socket for channel " << channel << " can not be created");
    }
    return NULL;
}
