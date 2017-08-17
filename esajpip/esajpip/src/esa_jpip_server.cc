#ifdef _PLATFORM_LINUX
#include <sys/prctl.h>
#endif

#include <sys/wait.h>

#include <signal.h>
#include "trace.h"
#include "version.h"
#include "app_info.h"
#include "app_config.h"
#include "args_parser.h"
#include "client_info.h"
#include "client_manager.h"
#include "net/poll_table.h"
#include "net/socket_stream.h"

using namespace std;
using namespace net;
using namespace jpeg2000;

#define SERVER_NAME       "ESA JPIP Server"
#define SERVER_APP_NAME   "esa_jpip_server"
#define CONFIG_FILE       "server.cfg"

#ifndef POLLRDHUP
#define POLLRDHUP         (0)
#endif

#define SNDBUF 524288

static AppConfig cfg;
static int base_id = 0;
static AppInfo app_info;
static Socket child_socket;
static PollTable poll_table;
static bool child_lost = false;
static IndexManager index_manager;
static UnixAddress child_address("/tmp/child_unix_address");
static UnixAddress father_address("/tmp/father_unix_address");

static int ChildProcess();

static void *ClientThread(void *arg);

bool ParseArguments(int argc, char **argv);

static void SIGCHLD_handler(int signal) {
    wait(NULL);
    child_lost = true;
}

int main(int argc, char **argv) {
    int res, fd;
    char cfgMark;
    string cfgName;
    Socket father_socket;
    InetAddress from_addr;
    InetAddress listen_addr;
    Socket listen_socket, new_conn;

    if (!app_info.Init())
        return CERR("The shared information can not be set");

    cfgMark = '-';
    cfgName = CONFIG_FILE;
    if (File::Exists("/etc/esajpip/" CONFIG_FILE)) {
        cfgName = "/etc/esajpip/" CONFIG_FILE;
        cfgMark = ' ';
    }

    if (!cfg.Load(cfgName.c_str()))
        return CERR("The configuration file '" << cfgName << "' can not be read");

    if (!ArgsParser(cfg, app_info).Parse(argc, argv))
        return -1;

    if (app_info.is_running())
        return CERR("The server is already running");

    if (!index_manager.Init(cfg.images_folder(), cfg.caching_folder()))
        return CERR("The index manager can not be initialized");

    app_info->father_pid = getpid();

    cout << endl << SERVER_NAME << " " << VERSION << endl;
    cout << endl << cfgMark << cfg << endl;

    if (cfg.logging())
        TraceSystem::AppendToFile(cfg.logging_folder() + SERVER_APP_NAME);

    if (!File::Exists(cfg.caching_folder().c_str()))
        ERROR("The cache folder does not exist");

    if (cfg.address().size() <= 0)
        listen_addr = InetAddress(cfg.port());
    else
        listen_addr = InetAddress(cfg.address().c_str(), cfg.port());

    if (!listen_socket.OpenInet())
        ERROR("The server listen socket can not be created");
    else if (!listen_socket.SetNoDelay() || !listen_socket.SetSndBuf(SNDBUF) || !listen_socket.ListenAt(listen_addr))
        ERROR("The server listen socket can not be initialized");
    else {
        LOG(SERVER_NAME << " started" << cfgMark);

        signal(SIGCHLD, SIG_IGN);

        poll_table.Add(listen_socket, POLLIN);

        if (!father_socket.OpenUnix(SOCK_DGRAM)) {
            ERROR("The father unix socket can not be created");
            return -1;
        }

        if (!father_socket.BindTo(father_address.Reset())) {
            ERROR("The father unix socket can not be bound");
            return -1;
        }

        poll_table.Add(father_socket, POLLIN);

        father_begin:

        if (!fork())
            return ChildProcess();
        else {
            signal(SIGCHLD, SIGCHLD_handler);

            for (;;) {
                res = poll_table.Poll();

                if (child_lost) {
                    child_lost = false;
                    goto father_begin;
                }

                if (res > 0) {
                    if (poll_table[0].revents & POLLIN) {
                        new_conn = listen_socket.Accept(&from_addr);

                        if (!new_conn.IsValid())
                            ERROR("Problems accepting a new connection");
                        else {
                            if (app_info->num_connections >= cfg.max_connections()) {
                                LOG("Refusing a connection because the limit has been reached");
                                new_conn.Close();
                            } else {
                                LOG("New connection from " << from_addr.GetPath() << ":" << from_addr.GetPort() << " ["
                                                           << (int) new_conn << "]");

                                if (!father_socket.SendDescriptor(child_address, new_conn, new_conn)) {
                                    ERROR("The new socket can not be sent to the child process");
                                    new_conn.Close();
                                } else {
                                    poll_table.Add(new_conn, POLLRDHUP | POLLERR | POLLHUP | POLLNVAL);
                                    app_info->num_connections++;
                                }
                            }
                        }
                    }

                    if (poll_table[1].revents & POLLIN) {
                        father_socket.Receive(&fd, sizeof(int));
                        LOG("Closing the connection [" << fd << "] from child");
                        app_info->num_connections--;
                        poll_table.Remove(fd);
                        close(fd);
                    }

                    for (int i = 2; i < poll_table.GetSize(); i++) {
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
        }
    }

    listen_socket.Close();

    return 0;
}

static int ChildProcess() {
    int sock, father_sock;
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

    for (int i = 2; i < poll_table.GetSize(); i++) {
        sock = poll_table[i].fd;
        client_info = new ClientInfo(0, sock, sock);

        LOG("Creating a client thread for the old connection [" << sock << "]");

        if (pthread_create(&service_tid, NULL, ClientThread, client_info) == -1) {
            ERROR("A new client thread for the old connection [" << sock << "] can not be created");
            delete client_info;
            return -1;
        }

        pthread_detach(service_tid);
    }

    for (;;) {
        child_socket.ReceiveDescriptor(&sock, &father_sock);
        client_info = new ClientInfo(base_id++, sock, father_sock);

        LOG("Creating a client thread for the new connection [" << sock << "|" << father_sock << "]");

        if (pthread_create(&service_tid, NULL, ClientThread, client_info) == -1) {
            LOG("A new client thread for the new connection [" << sock << "|" << father_sock << "] can not be created");
            delete client_info;
            return -1;
        }

        pthread_detach(service_tid);
    }

    return 0;
}

static void *ClientThread(void *arg) {
    int sock, res;
    ClientInfo *client_info = (ClientInfo *) arg;

#ifndef BASIC_SERVER
    ClientManager(cfg, app_info, index_manager).Run(client_info);
#else
    ClientManager(cfg, app_info, index_manager).RunBasic(client_info);
#endif

    sock = client_info->father_sock();
    res = child_socket.SendTo(father_address, &sock, sizeof(int));

    if (res < (int) sizeof(int))
        ERROR("The connection [" << sock << "] could not be closed");

    delete client_info;
    pthread_exit(0);
}
