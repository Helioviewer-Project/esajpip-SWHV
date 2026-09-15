#include <sys/socket.h>
#include <sys/wait.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <vector>
#include <unistd.h>

#include "trace.h"
#include "app_config.h"
#include "app_info.h"
#include "net/poll_table.h"
#include "net/socket.h"
#include "server/child.h"
#include "server/initial_request.h"
#include "server/parent.h"

using namespace std;
using namespace net;

#ifndef POLLRDHUP
#define POLLRDHUP (0)
#endif

using Clock = chrono::steady_clock;

struct Connection {
    uint64_t id;
    int fd;
    bool pending;
    Clock::time_point deadline;
};

static PollTable poll_table;
static vector<Connection> connections;
static uint64_t next_connection_id = 0;
static volatile sig_atomic_t child_lost = 0;

static void SIGCHLD_handler(int) {
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

static void CloseConnection(AppInfo &app_info, uint64_t id, const char *reason) {
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

static void ExpirePendingConnections(AppInfo &app_info) {
    Clock::time_point now = Clock::now();
    for (size_t i = 0; i < connections.size();) {
        if (connections[i].pending && connections[i].deadline <= now) {
            uint64_t id = connections[i].id;
            CloseConnection(app_info, id, "identification time-out");
        } else {
            ++i;
        }
    }
}

int RunParent(const AppConfig &cfg, AppInfo &app_info, Socket &listen_socket) {
    poll_table.Add(listen_socket, POLLIN);

    int control_fds[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, control_fds) != 0)
        return CERR("The child control socket can not be created: " << strerror(errno));
    Socket parent_socket(control_fds[0]);
    Socket child_socket(control_fds[1]);

    poll_table.Add(parent_socket, POLLIN);
    poll_table.Add(TraceSystem::ReadDescriptor(), POLLIN);

    struct sigaction child_action;
    memset(&child_action, 0, sizeof child_action);
    child_action.sa_handler = SIGCHLD_handler;
    sigemptyset(&child_action.sa_mask);
    child_action.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &child_action, NULL) != 0)
        return CERR("The child signal handler can not be installed: " << strerror(errno));

parent_begin:

    int parent_pipe[2];
    if (pipe(parent_pipe) != 0)
        return CERR("The parent lifetime pipe can not be created: " << strerror(errno));

    pid_t child_pid = fork();
    if (child_pid < 0) {
        close(parent_pipe[0]);
        close(parent_pipe[1]);
        parent_socket.Close();
        child_socket.Close();
        return CERR("The child process can not be created: " << strerror(errno));
    }
    if (child_pid == 0) {
        // The child must not inherit the only writer. Closing it before any
        // channel threads start makes the read end track the parent's lifetime.
        close(parent_pipe[1]);
        listen_socket.Close();
        parent_socket.Close();
        // The child keeps only the nonblocking end used to submit records.
        TraceSystem::CloseParentDescriptors();
        for (const Connection &connection : connections) {
            if (!connection.pending)
                shutdown(connection.fd, SHUT_RDWR);
            close(connection.fd);
        }
        connections.clear();
        return RunChild(cfg, parent_pipe[0], child_socket);
    }

    // The parent keeps the sole write end. The kernel closes it on every form
    // of parent exit, which wakes the child even when no signal can be caught.
    close(parent_pipe[0]);
    child_socket.Close();
    app_info->child_pid = child_pid;

    for (;;) {
        if (child_lost)
            break;

        int res = poll_table.Poll(GetPollTimeout());

        if (child_lost)
            break;

        if (res < 0 && errno != EINTR)
            ERROR("Connection poll failed: " << strerror(errno));

        bool network_ready = false;
        bool log_ready = false;
        if (res > 0) {
            network_ready = poll_table[0].revents || poll_table[1].revents;
            log_ready = poll_table[2].revents & POLLIN;
            for (int i = 3; i < poll_table.GetSize(); ++i)
                network_ready = network_ready || poll_table[i].revents;

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
                                           Clock::now() + chrono::seconds(cfg.initial_timeout())});
                    app_info->num_connections = static_cast<int>(connections.size());
                }
            }

            if (poll_table[1].revents & POLLIN) {
                uint64_t id;
                if (parent_socket.Receive(&id, sizeof id) == sizeof id)
                    CloseConnection(app_info, id, "client finished");
                else
                    ERROR("Could not receive connection identifier");
            }

            for (int i = 3; i < poll_table.GetSize();) {
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
                    InitialRequest request = InspectInitialRequest(fd);
                    if (request.state == REQUEST_REJECTED) {
                        CloseConnection(app_info, id, "not a JPIP client");
                        continue;
                    }
                    if (request.state == REQUEST_ACCEPTED) {
                        if (!parent_socket.SendDescriptor(fd, id)) {
                            ERROR("The JPIP socket can not be sent to the child process: " << strerror(errno));
                            CloseConnection(app_info, id, "dispatch failed");
                            continue;
                        }
                        connection->pending = false;
                        poll_table[i].events = POLLRDHUP | POLLERR | POLLHUP | POLLNVAL;
                        LOG("JPIP connection identified [" << fd << ":" << id << "]");
                    }
                }
                if (events & (POLLRDHUP | POLLERR | POLLHUP | POLLNVAL)) {
                    CloseConnection(app_info, id, "socket closed");
                    continue;
                }
                i++;
            }
        }

        ExpirePendingConnections(app_info);

        // Logging never takes precedence over connection work or deadlines.
        // Producers use a bounded nonblocking socket, while this parent is the
        // only process that performs log file I/O.
        if (log_ready && !network_ready)
            TraceSystem::DrainOne();
    }

    child_lost = 0;
    close(parent_pipe[1]);
    waitpid(child_pid, NULL, 0);

    parent_socket.Close();
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, control_fds) != 0)
        return CERR("The child control socket can not be recreated: " << strerror(errno));
    parent_socket = control_fds[0];
    child_socket = control_fds[1];
    poll_table[1].fd = parent_socket;
    poll_table[1].revents = 0;
    goto parent_begin;
}
