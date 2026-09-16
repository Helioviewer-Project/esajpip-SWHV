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
        int lifetime_pipe[2];
        if (pipe(lifetime_pipe) != 0) {
            cerr << "The supervisor lifetime pipe can not be created: "
                 << strerror(errno) << endl;
            return -1;
        }
        int crash_pipe[2];
        if (pipe(crash_pipe) != 0) {
            close(lifetime_pipe[0]);
            close(lifetime_pipe[1]);
            cerr << "The crash-report pipe can not be created: "
                 << strerror(errno) << endl;
            return -1;
        }

        pid_t child_pid = fork();
        if (child_pid < 0) {
            close(lifetime_pipe[0]);
            close(lifetime_pipe[1]);
            close(crash_pipe[0]);
            close(crash_pipe[1]);
            cerr << "The serving process can not be created: " << strerror(errno) << endl;
            return -1;
        }
        if (child_pid == 0) {
            close(lifetime_pipe[1]);
            close(crash_pipe[0]);
            ResetSignalHandler(SIGINT);
            ResetSignalHandler(SIGTERM);
            return RunServer(cfg, app_info, listen_socket, lifetime_pipe[0],
                             crash_pipe[1], log_name, description,
                             restart_message);
        }

        close(lifetime_pipe[0]);
        close(crash_pipe[1]);
        app_info->child_pid = child_pid;

        bool lifetime_closed = false;
        int status;
        for (;;) {
            if (stop_requested && !lifetime_closed) {
                close(lifetime_pipe[1]);
                lifetime_closed = true;
            }

            pid_t waited = waitpid(child_pid, &status, 0);
            if (waited == child_pid)
                break;
            if (waited < 0 && errno == EINTR)
                continue;

            if (!lifetime_closed)
                close(lifetime_pipe[1]);
            close(crash_pipe[0]);
            cerr << "The serving process can not be observed: " << strerror(errno) << endl;
            return -1;
        }

        if (!lifetime_closed)
            close(lifetime_pipe[1]);

        uint64_t crash_channel = numeric_limits<uint64_t>::max();
        ssize_t report_size;
        do {
            report_size = read(crash_pipe[0], &crash_channel, sizeof crash_channel);
        } while (report_size < 0 && errno == EINTR);
        close(crash_pipe[0]);

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
