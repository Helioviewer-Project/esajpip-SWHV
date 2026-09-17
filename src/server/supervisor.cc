#include <sys/socket.h>
#include <sys/wait.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <pthread.h>
#include <unistd.h>

#include "config.h"
#include "server/server.h"
#include "server/supervisor.h"

using namespace std;

namespace {

// macOS discards SIGCHLD under its default disposition even while it is
// blocked. A no-op disposition keeps it available to sigwait().
void ChildExited(int) {
}

}

int RunSupervisor(const Config &cfg, int listen_socket,
                  const string &log_name,
                  const string &description) {
    if (signal(SIGCHLD, ChildExited) == SIG_ERR) {
        cerr << "The child signal can not be configured: "
             << strerror(errno) << endl;
        return -1;
    }
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGCHLD);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    int signal_error = pthread_sigmask(SIG_BLOCK, &signals, NULL);
    if (signal_error != 0) {
        cerr << "The supervisor signals can not be blocked: "
             << strerror(signal_error) << endl;
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
            if (signal(SIGINT, SIG_IGN) == SIG_ERR ||
                signal(SIGTERM, SIG_IGN) == SIG_ERR) {
                cerr << "The serving-process stop signals can not be ignored: "
                     << strerror(errno) << endl;
                _exit(SERVER_STARTUP_FAILURE);
            }
            signal_error = pthread_sigmask(SIG_UNBLOCK, &signals, NULL);
            if (signal_error != 0) {
                cerr << "The serving-process signals can not be unblocked: "
                     << strerror(signal_error) << endl;
                _exit(SERVER_STARTUP_FAILURE);
            }
            int result = RunServer(cfg, listen_socket, supervisor_sockets[1],
                                   log_name, description,
                                   restart_message);
            _exit(result);
        }

        close(supervisor_sockets[1]);
        bool stopping = false;
        int status;
        for (;;) {
            pid_t waited = waitpid(child_pid, &status, WNOHANG);
            if (waited == child_pid)
                break;
            if (waited < 0) {
                if (!stopping)
                    close(supervisor_sockets[0]);
                cerr << "The serving process can not be observed: "
                     << strerror(errno) << endl;
                return -1;
            }

            int signal_number;
            signal_error = sigwait(&signals, &signal_number);

            if (signal_error != 0) {
                if (!stopping)
                    close(supervisor_sockets[0]);
                cerr << "The supervisor can not wait for signals: "
                     << strerror(signal_error) << endl;
                return -1;
            }
            if (signal_number == SIGINT || signal_number == SIGTERM) {
                if (!stopping)
                    close(supervisor_sockets[0]);
                stopping = true;
                continue;
            }
        }

        if (!stopping)
            close(supervisor_sockets[0]);

        if (stopping)
            return 0;
        if (WIFEXITED(status) && WEXITSTATUS(status) == SERVER_STARTUP_FAILURE)
            return -1;

        if (WIFSIGNALED(status))
            restart_message = "The serving process was killed; restarting";
        else
            restart_message = "The serving process exited unexpectedly; restarting";
        cerr << restart_message << endl;
    }
}
