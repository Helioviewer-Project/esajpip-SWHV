#include <cerrno>
#include <cstdlib>
#include <string>

#include "config.h"
#include "net/address.h"
#include "server/server.h"
#include "trace.h"

using namespace std;

#define SERVER_VERSION  "2.0-rc1"
#define SERVER_NAME     "ESA JPIP Server"
#define SERVER_LOG_NAME "esajpip"
#define CONFIG_FILE     "server.ini"

int main(int argc, char **argv) {
    if (argc > 1)
        return CERR("Invalid command");

    Config cfg;
    string config_error;
    if (!cfg.Load(CONFIG_FILE, config_error))
        return CERR("Configuration error in '" << CONFIG_FILE << "': " << config_error);

    long worker_threads = 16;
    const char *pool_text = getenv("UV_THREADPOOL_SIZE");
    if (pool_text == NULL) {
        if (setenv("UV_THREADPOOL_SIZE", "16", 0) != 0)
            return CERR("The worker-pool size can not be configured");
    } else {
        bool decimal = *pool_text != '\0';
        for (const char *p = pool_text; *p != '\0'; ++p)
            if (*p < '0' || *p > '9')
                decimal = false;
        char *end;
        errno = 0;
        worker_threads = strtol(pool_text, &end, 10);
        if (!decimal || errno != 0 || *end != '\0' ||
            worker_threads < 2 || worker_threads > 1024)
            return CERR("UV_THREADPOOL_SIZE must be an integer from 2 to 1024");
    }

    cout << '\n' << SERVER_NAME << ' ' << SERVER_VERSION << "\n\n" << cfg;

    net::InetAddress listen_addr = cfg.address().empty()
                                       ? net::InetAddress(cfg.port())
                                       : net::InetAddress(cfg.address().c_str(), cfg.port());
    if (!listen_addr.IsValid())
        return CERR("The listen address '" << cfg.address() << "' can not be resolved");

    string log_name = cfg.file_logging()
            ? cfg.log_directory() + SERVER_LOG_NAME + "." +
                    listen_addr.GetPath() + "." + to_string(listen_addr.GetPort())
            : "";
    string description = string(SERVER_NAME) + " " + SERVER_VERSION;
    return RunServer(cfg, listen_addr, log_name, description,
                     static_cast<unsigned int>(worker_threads));
}
