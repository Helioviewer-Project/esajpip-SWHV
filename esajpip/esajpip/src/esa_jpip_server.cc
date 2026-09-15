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
#include <vector>
#include "trace.h"
#include "app_info.h"
#include "app_config.h"
#include "args_parser.h"
#include "client_manager.h"
#include "net/poll_table.h"
#include "net/socket.h"

using namespace std;
using namespace net;

#define SERVER_VERSION    "1.9.0-rc2"
#define SERVER_NAME       "ESA JPIP Server"
#define SERVER_APP_NAME   "esa_jpip_server"
#define CONFIG_FILE       "server.cfg"

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

struct ClientInfo {
    uint64_t id;
    int fd;
};

enum AdmissionResult {
    ADMISSION_PENDING,
    ADMISSION_ACCEPTED,
    ADMISSION_REJECTED
};

static const size_t MAX_REQUEST_LINE = 2048;

static AppConfig cfg;
static AppInfo app_info;
static Socket child_socket;
static PollTable poll_table;
static vector<Connection> connections;
static uint64_t next_connection_id = 0;
static volatile sig_atomic_t child_lost = 0;
static UnixAddress child_address("/tmp/child_unix_address");
static UnixAddress father_address("/tmp/father_unix_address");

static int ChildProcess(const pthread_attr_t *pattr);
static void StartClient(const pthread_attr_t *pattr, uint64_t id, int fd);

static void *ClientThread(void *arg);

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

static bool HasParameter(const char *begin, const char *end, const char *name, size_t name_length) {
    while (begin < end) {
        const char *separator = static_cast<const char *>(memchr(begin, '&', end - begin));
        const char *parameter_end = separator ? separator : end;
        const char *equals = static_cast<const char *>(memchr(begin, '=', parameter_end - begin));
        const char *parameter_name_end = equals ? equals : parameter_end;
        if (static_cast<size_t>(parameter_name_end - begin) == name_length &&
            memcmp(begin, name, name_length) == 0)
            return true;
        if (!separator)
            break;
        begin = separator + 1;
    }
    return false;
}

static AdmissionResult CheckAdmission(int fd) {
    char line[MAX_REQUEST_LINE];
    ssize_t length;
    do {
        length = recv(fd, line, sizeof line, MSG_PEEK | MSG_DONTWAIT);
    } while (length < 0 && errno == EINTR);

    if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return ADMISSION_PENDING;
    if (length <= 0)
        return ADMISSION_REJECTED;

    static const char method[] = "GET ";
    size_t prefix_length = min(static_cast<size_t>(length), sizeof method - 1);
    if (memcmp(line, method, prefix_length) != 0)
        return ADMISSION_REJECTED;
    if (static_cast<size_t>(length) < sizeof method - 1)
        return ADMISSION_PENDING;

    const char *newline = static_cast<const char *>(memchr(line, '\n', length));
    if (newline == NULL)
        return static_cast<size_t>(length) == sizeof line ? ADMISSION_REJECTED : ADMISSION_PENDING;

    const char *uri = line + sizeof method - 1;
    const char *uri_end = static_cast<const char *>(memchr(uri, ' ', newline - uri));
    if (!uri_end)
        return ADMISSION_REJECTED;

    const char *protocol = uri_end;
    while (protocol < newline && *protocol == ' ')
        ++protocol;
    if (newline - protocol < 8 ||
        (memcmp(protocol, "HTTP/1.0", 8) != 0 && memcmp(protocol, "HTTP/1.1", 8) != 0))
        return ADMISSION_REJECTED;

    const char *query = static_cast<const char *>(memchr(uri, '?', uri_end - uri));
    return query && HasParameter(query + 1, uri_end, "cnew", 4)
               ? ADMISSION_ACCEPTED
               : ADMISSION_REJECTED;
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

    if (cfg.logging())
        TraceSystem::AppendToFile(cfg.logging_folder() + SERVER_APP_NAME);

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
        return CERR("The client thread attributes can not be initialized: " << strerror(pthread_error));
    pthread_error = pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);
    if (pthread_error != 0) {
        pthread_attr_destroy(&pattr);
        return CERR("Detached client threads can not be configured: " << strerror(pthread_error));
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
                                           Clock::now() + chrono::seconds(cfg.identification_time_out())});
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
                    AdmissionResult admission = CheckAdmission(fd);
                    if (admission == ADMISSION_REJECTED) {
                        CloseConnection(id, "not a JPIP client");
                        continue;
                    }
                    if (admission == ADMISSION_ACCEPTED) {
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

    for (const Connection &connection : connections) {
        if (connection.pending)
            close(connection.fd);
        else
            StartClient(pattr, connection.id, connection.fd);
    }
    connections.clear();

    for (;;) {
        int fd;
        uint64_t id;
        if (!child_socket.ReceiveDescriptor(&fd, &id)) {
            ERROR("The admitted socket can not be received by the child process: " << strerror(errno));
            continue;
        }
        LOG("Creating a client thread for connection [" << id << "]");
        StartClient(pattr, id, fd);
    }

    return 0;
}

static void NotifyParent(uint64_t id) {
    if (child_socket.SendTo(father_address, &id, sizeof id) == sizeof id)
        return;
    ERROR("The completed connection [" << id << "] could not notify the parent");
}

static void StartClient(const pthread_attr_t *pattr, uint64_t id, int fd) {
    ClientInfo *client_info = new ClientInfo{id, fd};
    pthread_t service_tid;
    int pthread_error = pthread_create(&service_tid, pattr, ClientThread, client_info);
    if (pthread_error == 0)
        return;

    ERROR("A client thread for connection [" << id << "] can not be created: "
          << strerror(pthread_error));
    shutdown(fd, SHUT_RDWR);
    close(fd);
    NotifyParent(id);
    delete client_info;
}

static void *ClientThread(void *arg) {
    ClientInfo *client_info = static_cast<ClientInfo *>(arg);
    uint64_t id = client_info->id;
    Socket socket(client_info->fd);

    RunClient(cfg, socket, id);

    shutdown(socket, SHUT_RDWR);
    socket.Close();
    NotifyParent(id);

    delete client_info;
    return NULL;
}
