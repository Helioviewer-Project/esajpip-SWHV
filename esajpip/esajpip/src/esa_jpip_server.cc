#ifdef _PLATFORM_LINUX
#include <sys/prctl.h>
#endif

#include <sys/wait.h>

#include <csignal>
#include "trace.h"
#include "app_info.h"
#include "app_config.h"
#include "args_parser.h"
#include "client_info.h"
#include "client_manager.h"
#include "net/poll_table.h"
#include "net/socket_stream.h"

using namespace std;
using namespace net;

#define SERVER_VERSION    "1.8.2"
#define SERVER_NAME       "ESA JPIP Server"
#define SERVER_APP_NAME   "esa_jpip_server"
#define CONFIG_FILE       "server.cfg"

#ifndef POLLRDHUP
#define POLLRDHUP         (0)
#endif

static AppConfig cfg;
static int base_id = 0;
static AppInfo app_info;
static Socket child_socket;
static PollTable poll_table;
static bool child_lost = false;
static UnixAddress child_address("/tmp/child_unix_address");
static UnixAddress father_address("/tmp/father_unix_address");

static int ChildProcess(const pthread_attr_t *pattr);

static void *ClientThread(void *arg);

static void SIGCHLD_handler(int signal) {
    wait(NULL);
    child_lost = true;
}

int main(int argc, char **argv) {
    if (!app_info.Init())
        return CERR("The shared information can not be set");
    if (!cfg.Load(CONFIG_FILE))
        return CERR("The configuration file '" << CONFIG_FILE << "' can not be read");
    if (!ArgsParser(cfg, app_info).Parse(argc, argv))
        return -1;
    if (app_info.is_running())
        return CERR("The server is already running");

    app_info->father_pid = getpid();

    cout << endl << SERVER_NAME << " " << SERVER_VERSION << endl;
    cout << endl << '-' << cfg << endl;

    if (cfg.logging())
        TraceSystem::AppendToFile(cfg.logging_folder() + SERVER_APP_NAME);

    int max_connections = cfg.max_connections();

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
    pthread_attr_init(&pattr);
    pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);

father_begin:

    if (!fork())
        return ChildProcess(&pattr);

    // in father
    signal(SIGCHLD, SIGCHLD_handler);

    for (;;) {
        int res = poll_table.Poll();

        if (child_lost) {
            child_lost = false;
            goto father_begin;
        }

        if (res > 0) {
            if (poll_table[0].revents & POLLIN) {
                InetAddress from_addr;
                Socket new_conn = listen_socket.Accept(&from_addr);

                if (new_conn == -1) {
                    ERROR("Error accepting a new connection: " << strerror(errno));
                } else if (app_info->num_connections >= max_connections) {
                    LOG("Connection refused because the limit has been reached");
                    new_conn.Close();
                } else {
                    LOG("New connection from " << from_addr.GetPath() << ":" << from_addr.GetPort() << " [" << (int) new_conn << "]");

                    if (!father_socket.SendDescriptor(child_address, new_conn, new_conn)) {
                        ERROR("The new socket can not be sent to the child process: " << strerror(errno));
                        new_conn.Close();
                    } else {
                        poll_table.Add(new_conn, POLLRDHUP | POLLERR | POLLHUP | POLLNVAL);
                        app_info->num_connections++;
                    }
                }
            }

            if (poll_table[1].revents & POLLIN) {
                int fd;
                if (father_socket.Receive(&fd, sizeof fd) == sizeof fd) {
                    LOG("Closing the connection [" << fd << "] from child");
                    app_info->num_connections--;
                    close(fd);
                    poll_table.Remove(fd);
                } else
                    ERROR("Could not receive descriptor");
            }

            for (int i = 2; i < poll_table.GetSize(); ++i) {
                if (poll_table[i].revents) {
                    LOG("Closing the connection [" << poll_table[i].fd << "]");
                    app_info->num_connections--;
                    close(poll_table[i].fd);
                    poll_table.RemoveAt(i);
                }
            }

            if (app_info->num_connections < 0)
                app_info->num_connections = 0;
        }
    }

    pthread_attr_destroy(&pattr);

    listen_socket.Close();
    return 0;
}

static int ChildProcess(const pthread_attr_t *pattr) {
    int sock;
    pthread_t service_tid;
    ClientInfo *client_info;

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

    for (int i = 2; i < poll_table.GetSize(); ++i) {
        sock = poll_table[i].fd;
        client_info = new ClientInfo(0, sock, sock);

        LOG("Creating a client thread for the old connection [" << sock << "]");

        if (pthread_create(&service_tid, pattr, ClientThread, client_info) == -1) {
            ERROR("A new client thread for the old connection [" << sock << "] can not be created");
            delete client_info;
            return -1;
        }
    }

    for (;;) {
        int father_sock;
        if (!child_socket.ReceiveDescriptor(&sock, &father_sock)) {
            ERROR("The new socket can not be received by the child process: " << strerror(errno));
            continue;
        }
        client_info = new ClientInfo(base_id++, sock, father_sock);

        LOG("Creating a client thread for the new connection [" << sock << "|" << father_sock << "]");

        if (pthread_create(&service_tid, pattr, ClientThread, client_info) == -1) {
            LOG("A new client thread for the new connection [" << sock << "|" << father_sock << "] can not be created");
            delete client_info;
            return -1;
        }
    }

    return 0;
}

static void *ClientThread(void *arg) {
    ClientInfo *client_info = (ClientInfo *) arg;

    ClientManager(cfg, app_info).Run(client_info);

    int sock = client_info->father_sock();
    if (child_socket.SendTo(father_address, &sock, sizeof sock) != sizeof sock)
        ERROR("The connection [" << sock << "] could not be closed");

    delete client_info;
    pthread_exit(NULL);
    return NULL;
}
