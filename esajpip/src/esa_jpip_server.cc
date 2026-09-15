#include <sys/socket.h>
#include <sys/wait.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <vector>
#include "trace.h"
#include "app_info.h"
#include "app_config.h"
#include "args_parser.h"
#include "net/poll_table.h"
#include "net/socket.h"
#include "server/child.h"
#include "server/initial_request.h"

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

static AppConfig cfg;
static AppInfo app_info;
static PollTable poll_table;
static vector<Connection> connections;
static uint64_t next_connection_id = 0;
static volatile sig_atomic_t child_lost = 0;
static UnixAddress child_address("/tmp/child_unix_address");
static UnixAddress father_address("/tmp/father_unix_address");

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

    struct sigaction child_action;
    memset(&child_action, 0, sizeof child_action);
    child_action.sa_handler = SIGCHLD_handler;
    sigemptyset(&child_action.sa_mask);
    child_action.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &child_action, NULL) != 0)
        return CERR("The child signal handler can not be installed: " << strerror(errno));

father_begin:

    int parent_pipe[2];
    if (pipe(parent_pipe) != 0)
        return CERR("The parent lifetime pipe can not be created: " << strerror(errno));

    pid_t child_pid = fork();
    if (child_pid < 0) {
        close(parent_pipe[0]);
        close(parent_pipe[1]);
        return CERR("The child process can not be created: " << strerror(errno));
    }
    if (child_pid == 0) {
        // The child must not inherit the only writer. Closing it before any
        // channel threads start makes the read end track the parent's lifetime.
        close(parent_pipe[1]);
        listen_socket.Close();
        father_socket.Close();
        for (const Connection &connection : connections) {
            if (!connection.pending)
                shutdown(connection.fd, SHUT_RDWR);
            close(connection.fd);
        }
        connections.clear();
        return RunChild(cfg, parent_pipe[0], child_address, father_address);
    }

    // The parent keeps the sole write end. The kernel closes it on every form
    // of parent exit, which wakes the child even when no signal can be caught.
    close(parent_pipe[0]);
    app_info->child_pid = child_pid;

    for (;;) {
        if (child_lost)
            break;

        int res = poll_table.Poll(GetPollTimeout());

        if (child_lost)
            break;

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
                                           Clock::now() + chrono::seconds(cfg.initial_timeout())});
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
                    InitialRequest request = InspectInitialRequest(fd);
                    if (request.state == REQUEST_REJECTED) {
                        CloseConnection(id, "not a JPIP client");
                        continue;
                    }
                    if (request.state == REQUEST_ACCEPTED) {
                        if (!father_socket.SendDescriptor(child_address, fd, id)) {
                            ERROR("The JPIP socket can not be sent to the child process: " << strerror(errno));
                            CloseConnection(id, "dispatch failed");
                            continue;
                        }
                        connection->pending = false;
                        poll_table[i].events = POLLRDHUP | POLLERR | POLLHUP | POLLNVAL;
                        LOG("JPIP connection identified [" << fd << ":" << id << "]");
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

    child_lost = 0;
    close(parent_pipe[1]);
    waitpid(child_pid, NULL, 0);
    goto father_begin;
}
