#include <cstring>
#include <string>
#include <unistd.h>

#include "trace.h"
#include "app_config.h"
#include "app_info.h"
#include "net/socket.h"
#include "server/parent.h"

using namespace std;
using namespace net;

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
    if (!cfg.Load(CONFIG_FILE))
        return CERR("The configuration file '" << CONFIG_FILE << "' can not be read");

    app_info->parent_pid = getpid();

    cout << endl << SERVER_NAME << " " << SERVER_VERSION << endl;
    cout << endl << '-' << cfg << endl;

    string log_name = cfg.file_logging() ? cfg.log_directory() + SERVER_APP_NAME : "";
    if (!TraceSystem::Initialize(log_name))
        return CERR("The logging system can not be initialized");

    Socket listen_socket;
    InetAddress listen_addr = cfg.address().empty()
                                  ? InetAddress(cfg.port())
                                  : InetAddress(cfg.address().c_str(), cfg.port());
    if (!listen_socket.OpenInet())
        return CERR("The server listen socket can not be created");
    if (!listen_socket.ListenAt(listen_addr))
        return CERR("The server listen socket can not be initialized");

    LOG(SERVER_NAME << " " << SERVER_VERSION << " started");
    return RunParent(cfg, app_info, listen_socket);
}
