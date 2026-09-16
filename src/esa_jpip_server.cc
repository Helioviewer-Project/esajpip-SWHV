#include <cerrno>
#include <cstring>
#include <string>
#include <unistd.h>

#include "trace.h"
#include "app_config.h"
#include "app_info.h"
#include "net/socket.h"
#include "server/supervisor.h"

using namespace std;

#define SERVER_VERSION  "2.0-rc1"
#define SERVER_NAME     "ESA JPIP Server"
#define SERVER_APP_NAME "esa_jpip_server"
#define CONFIG_FILE     "server.ini"

int main(int argc, char **argv) {
    AppInfo app_info;
    if (!app_info.Init())
        return CERR("The server status can not be initialized");
    if (argc > 1) {
        if (argc == 2 && strcmp(argv[1], "status") == 0) {
            cout << app_info;
            return 0;
        }
        return CERR("Invalid command");
    }
    if (app_info.is_running())
        return CERR("The server is already running");

    AppConfig cfg;
    string config_error;
    if (!cfg.Load(CONFIG_FILE, config_error))
        return CERR("Configuration error in '" << CONFIG_FILE << "': " << config_error);

    app_info->parent_pid = getpid();

    cout << endl << SERVER_NAME << " " << SERVER_VERSION << endl;
    cout << endl << '-' << cfg << endl;

    net::Socket listen_socket;
    net::InetAddress listen_addr = cfg.address().empty()
                                       ? net::InetAddress(cfg.port())
                                       : net::InetAddress(cfg.address().c_str(), cfg.port());
    if (!listen_socket.OpenInet())
        return CERR("The server listen socket can not be created: " << strerror(errno));
    if (!listen_socket.ListenAt(listen_addr))
        return CERR("The server listen socket can not be initialized: " << strerror(errno));

    string log_name = cfg.file_logging() ? cfg.log_directory() + SERVER_APP_NAME : "";
    string description = string(SERVER_NAME) + " " + SERVER_VERSION;
    return RunSupervisor(cfg, app_info, listen_socket, log_name, description);
}
