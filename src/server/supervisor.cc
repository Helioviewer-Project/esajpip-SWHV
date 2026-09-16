#include <sys/wait.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
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

    for (;;) {
        int lifetime_pipe[2];
        if (pipe(lifetime_pipe) != 0) {
            cerr << "The supervisor lifetime pipe can not be created: "
                 << strerror(errno) << endl;
            return -1;
        }

        pid_t child_pid = fork();
        if (child_pid < 0) {
            close(lifetime_pipe[0]);
            close(lifetime_pipe[1]);
            cerr << "The serving process can not be created: " << strerror(errno) << endl;
            return -1;
        }
        if (child_pid == 0) {
            close(lifetime_pipe[1]);
            ResetSignalHandler(SIGINT);
            ResetSignalHandler(SIGTERM);
            return RunServer(cfg, app_info, listen_socket, lifetime_pipe[0],
                             log_name, description);
        }

        close(lifetime_pipe[0]);
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
            cerr << "The serving process can not be observed: " << strerror(errno) << endl;
            return -1;
        }

        if (!lifetime_closed)
            close(lifetime_pipe[1]);
        app_info->child_pid = 0;
        app_info->num_connections = 0;

        if (stop_requested)
            return 0;
        if (WIFEXITED(status) && WEXITSTATUS(status) == SERVER_STARTUP_FAILURE)
            return -1;

        if (WIFSIGNALED(status)) {
            cerr << "The serving process ended on signal " << WTERMSIG(status)
                 << "; restarting" << endl;
        } else {
            cerr << "The serving process exited with status "
                 << (WIFEXITED(status) ? WEXITSTATUS(status) : -1)
                 << "; restarting" << endl;
        }
    }
}
