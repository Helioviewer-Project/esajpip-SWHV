#include <string>
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>

#include "trace.h"
#include "args_parser.h"

using namespace std;

bool ParseArgs(AppInfo &app_info, int argc, char **argv) {
    if (argc <= 1) return true;
    else {
        bool res = false;
        string argv1 = argv[1];

        if (argv1 == "stop") {
            if (!app_info.is_running()) {
                CERR("The server is not running");
            } else if (argc == 2) {
                kill(app_info->father_pid, SIGKILL);
                waitpid(app_info->father_pid, NULL, 0);
                kill(app_info->child_pid, SIGKILL);
                waitpid(app_info->child_pid, NULL, 0);
            } else {
                CERR("Invalid command");
            }
        } else if (argv1 == "status") {
            cout << app_info;
        } else {
            CERR("Invalid command");
        }

        return res;
    }
}
