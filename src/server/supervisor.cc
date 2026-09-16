#include <sys/socket.h>
#include <sys/wait.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <unistd.h>

#include "app_config.h"
#include "app_info.h"
#include "net/socket.h"
#include "server/server.h"
#include "server/supervisor.h"

using namespace std;

namespace {

volatile sig_atomic_t stop_requested = 0;

void RequestStop(int) {
    stop_requested = 1;
}

bool InstallSignalHandler(int signal_number) {
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = RequestStop;
    sigemptyset(&action.sa_mask);
    return sigaction(signal_number, &action, NULL) == 0;
}

void ResetSignalHandler(int signal_number) {
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(signal_number, &action, NULL);
}

}

int RunSupervisor(const AppConfig &cfg, AppInfo &app_info,
                  net::Socket &listen_socket, const string &log_name,
                  const string &description) {
    if (!InstallSignalHandler(SIGINT) || !InstallSignalHandler(SIGTERM)) {
        cerr << "The supervisor signal handlers can not be installed: "
             << strerror(errno) << endl;
        return -1;
    }

    string restart_message;
    for (;;) {
        int supervisor_sockets[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, supervisor_sockets) != 0) {
            cerr << "The supervisor socket pair can not be created: "
                 << strerror(errno) << endl;
            return -1;
        }

        pid_t child_pid = fork();
        if (child_pid < 0) {
            close(supervisor_sockets[0]);
            close(supervisor_sockets[1]);
            cerr << "The serving process can not be created: " << strerror(errno) << endl;
            return -1;
        }
        if (child_pid == 0) {
            close(supervisor_sockets[0]);
            ResetSignalHandler(SIGINT);
            ResetSignalHandler(SIGTERM);
            return RunServer(cfg, app_info, listen_socket, supervisor_sockets[1],
                             log_name, description, restart_message);
        }

        close(supervisor_sockets[1]);
        app_info->child_pid = child_pid;

        bool supervisor_closed = false;
        int status;
        for (;;) {
            if (stop_requested && !supervisor_closed) {
                close(supervisor_sockets[0]);
                supervisor_closed = true;
            }

            pid_t waited = waitpid(child_pid, &status, 0);
            if (waited == child_pid)
                break;
            if (waited < 0 && errno == EINTR)
                continue;

            if (!supervisor_closed)
                close(supervisor_sockets[0]);
            cerr << "The serving process can not be observed: " << strerror(errno) << endl;
            return -1;
        }

        uint64_t crash_channel = numeric_limits<uint64_t>::max();
        ssize_t report_size = 0;
        if (!supervisor_closed) {
            do {
                report_size = read(supervisor_sockets[0], &crash_channel,
                                   sizeof crash_channel);
            } while (report_size < 0 && errno == EINTR);
            close(supervisor_sockets[0]);
        }

        app_info->child_pid = 0;
        app_info->num_connections = 0;

        if (stop_requested)
            return 0;
        if (WIFEXITED(status) && WEXITSTATUS(status) == SERVER_STARTUP_FAILURE)
            return -1;

        ostringstream message;
        message << "The serving process died";
        if (report_size == sizeof crash_channel &&
            crash_channel != numeric_limits<uint64_t>::max())
            message << " in channel " << crash_channel;
        message << "; restarting";
        restart_message = message.str();
        cerr << restart_message << endl;
    }
}
