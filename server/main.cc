#include <algorithm>
#include <string>
#include <thread>

#include "config.h"
#include "server.h"
#include "trace.h"

using namespace std;

#define SERVER_VERSION  "2.0-rc1"
#define SERVER_NAME     "ESA JPIP Server"
#define SERVER_LOG_NAME "esajpip"
#define CONFIG_FILE     "server.ini"

// Opening an image is blocking file work, so the number opened at once does
// not follow the processor count.
const unsigned int IMAGE_OPEN_THREADS = 8;

int main(int argc, char **argv) {
    if (argc > 1)
        return CERR("Invalid command");

    server::Config cfg;
    string config_error;
    if (!cfg.Load(CONFIG_FILE, config_error))
        return CERR("Configuration error in '" << CONFIG_FILE << "': " << config_error);

    cout << '\n' << SERVER_NAME << ' ' << SERVER_VERSION << "\n\n" << cfg;

    string log_name = cfg.file_logging()
            ? cfg.log_directory() + SERVER_LOG_NAME + "." +
                    (cfg.address().empty() ? "0.0.0.0" : cfg.address()) + "." +
                    to_string(cfg.port())
            : "";
    string description = string(SERVER_NAME) + " " + SERVER_VERSION;
    // Responses are processor work, so one I/O thread per hardware thread.
    return server::RunServer(cfg, log_name, description,
                             max(1u, thread::hardware_concurrency()),
                             IMAGE_OPEN_THREADS);
}
