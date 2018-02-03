#include <string>
#include <iomanip>
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#ifndef _NO_DIRENT
#include <dirent.h>
#endif

#include "trace.h"
#include "args_parser.h"

using namespace std;

bool ArgsParser::Parse(int argc, char **argv) {
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
        } else if (argv1 == "record") {
            static double cpu = 0;
            unsigned long tm_after, tm_before;

            cout << "Time\t\t\t Alive?\tFree\tFather\tChild\tConns\tIters\tThreads\t%CPU" << endl;

            app_info.Update();

            if (argv2 != "") TraceSystem::AppendToFile(argv2.c_str());

            for (;;) {
                app_info.Update();
                tm_before = app_info.child_time();

                if (!app_info.is_running())
                    LOG("0" << "\t" << app_info.available_memory() << "\t0\t0\t0\t0\t0\t0");
                else
                    LOG("1" << setiosflags(ios::fixed) << "\t"
                            << setprecision(2)
                            << app_info.available_memory() << "\t"
                            << app_info.father_memory() << "\t"
                            << app_info.child_memory() << "\t"
                            << app_info->num_connections << "\t"
                            << app_info->child_iterations << "\t"
                            << app_info.num_threads() << "\t"
                            << setiosflags(ios::fixed) << cpu
                    );

                sleep(5);
                app_info.Update();
                tm_after = app_info.child_time();
                cpu = (tm_after - tm_before) / 5.0;
            }
        } else {
            CERR("Invalid command");
        }

        return res;
    }
}
