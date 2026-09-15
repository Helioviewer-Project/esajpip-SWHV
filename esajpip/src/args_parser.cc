#include <string>
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "trace.h"
#include "args_parser.h"

using namespace std;

bool ParseArgs(AppInfo &app_info, int argc, char **argv) {
    if (argc <= 1) return true;
    else {
        bool res = false;
        string argv1 = argv[1];
        string argv2 = (argc > 2 ? argv[2] : "");

        if (argv1 == "stop") {
            if (!app_info.is_running()) {
                CERR("The server is not running");
            } else if (argc == 2) {
                kill(app_info->father_pid, SIGKILL);
                waitpid(app_info->father_pid, NULL, 0);
                kill(app_info->child_pid, SIGKILL);
                waitpid(app_info->child_pid, NULL, 0);
            } else if (argv2 == "child") {
                kill(app_info->child_pid, SIGKILL);
                waitpid(app_info->child_pid, NULL, 0);
            } else {
                CERR("Invalid command");
            }
        } else if (argv1 == "debug") {
            if (!app_info.is_running()) {
                CERR("The server is not running");

            } else {
                stringstream cmd;

                cmd << "echo continue > /tmp/gdb_command;gdb " << argv[0] << " "
                    << (argv2 == "child" ? app_info->child_pid : app_info->father_pid)
                    << " -x /tmp/gdb_command";

                system(cmd.str().c_str());
            }
        } else if (argv1 == "start") {
            res = true;
        } else if (argv1 == "status") {
            app_info.Update();
            cout << app_info;
        } else {
            CERR("Invalid command");
        }

        return res;
    }
}
