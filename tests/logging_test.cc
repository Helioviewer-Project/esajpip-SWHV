#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "trace.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static string ReadFile(const string &name) {
    ifstream input(name.c_str());
    return string((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
}

static string FindLog(const string &directory, const string &prefix) {
    DIR *files = opendir(directory.c_str());
    Check(files != NULL, "Could not list logging test files");
    string result;
    for (dirent *entry = readdir(files); entry != NULL; entry = readdir(files)) {
        string name = entry->d_name;
        if (name.compare(0, prefix.size(), prefix) == 0 &&
            name.size() >= 4 && name.compare(name.size() - 4, 4, ".log") == 0) {
            result = directory + "/" + name;
            break;
        }
    }
    closedir(files);
    return result;
}

int main() {
    char directory[] = "/tmp/esajpip-log-XXXXXX";
    Check(mkdtemp(directory) != NULL, "Could not create logging test directory");
    string base = string(directory) + "/server";
    Check(trace::Initialize(base), "Could not initialize logging");

    LOG("first message");
    LOG("second message");
    trace::Flush();

    thread worker([] {
        LOG("worker message");
    });
    worker.join();
    trace::Flush();

    LOG(string(2000, 'x'));
    trace::Flush();
    LOG("message after rollover");
    trace::Flush();

    string active = FindLog(directory, "server.");
    Check(!active.empty(), "Active log was not created");
    string backup = active + ".1";

    string old_messages = ReadFile(backup);
    string new_messages = ReadFile(active);
    Check(old_messages.find("first message") != string::npos,
          "First message missing from rolled log");
    Check(old_messages.find("first message\n") != string::npos,
          "Log line contains trailing characters");
    Check(old_messages.find("second message") != string::npos,
          "Second message missing from rolled log");
    Check(old_messages.find("worker message") != string::npos,
          "Worker message missing from rolled log");
    Check(old_messages.find(string(1900, 'x') + "...\n") != string::npos,
          "Oversized log message was not marked as truncated");
    Check(new_messages.find("message after rollover") != string::npos,
          "Message missing after rollover");

    {
        vector<thread> producers;
        for (int producer = 0; producer < 8; ++producer) {
            producers.emplace_back([producer] {
                for (int i = 0; i < 2000; ++i)
                    LOG("concurrent message " << producer << ':' << i);
            });
        }
        for (thread &producer : producers)
            producer.join();
    }
    trace::Flush();
    LOG("message after queue saturation");
    trace::Flush();
    new_messages = ReadFile(active);
    old_messages = ReadFile(backup);
    Check(new_messages.find("log messages dropped") != string::npos ||
                  old_messages.find("log messages dropped") != string::npos,
          "Dropped log messages were not reported");
    Check(new_messages.find("message after queue saturation") != string::npos ||
                  old_messages.find("message after queue saturation") != string::npos,
          "Logging did not recover after queue saturation");

    unlink(backup.c_str());
    Check(mkdir(backup.c_str(), 0700) == 0, "Could not obstruct log rollover");
    LOG(string(1100, 'x'));
    trace::Flush();
    int active_fd = open(active.c_str(), O_RDONLY);
    Check(active_fd >= 0, "Could not open the disabled log");
    off_t disabled_size = lseek(active_fd, 0, SEEK_END);
    close(active_fd);
    Check(disabled_size >= 0, "Could not measure the disabled log");

    LOG("message after failed rollover");
    trace::Flush();
    active_fd = open(active.c_str(), O_RDONLY);
    Check(active_fd >= 0, "Could not reopen the disabled log");
    Check(lseek(active_fd, 0, SEEK_END) == disabled_size,
          "Logging continued after rollover failure");
    close(active_fd);

    trace::Drain();
    Check(rmdir(backup.c_str()) == 0, "Could not remove rollover obstruction");

    string shutdown_base = string(directory) + "/shutdown";
    Check(trace::Initialize(shutdown_base),
          "Could not reinitialize logging for shutdown");
    LOG("message queued before shutdown");
    trace::Drain();
    string shutdown_log = FindLog(directory, "shutdown.");
    Check(!shutdown_log.empty() &&
                  ReadFile(shutdown_log).find("message queued before shutdown") !=
                      string::npos,
          "Logger shutdown did not drain queued records");

    int broken_output[2];
    Check(pipe(broken_output) == 0,
          "Could not create the failed-output test pipe");
    close(broken_output[0]);
    pid_t child = fork();
    Check(child >= 0, "Could not fork the failed-output test");
    if (child == 0) {
        signal(SIGPIPE, SIG_IGN);
        if (dup2(broken_output[1], STDOUT_FILENO) < 0)
            _exit(2);
        if (broken_output[1] != STDOUT_FILENO)
            close(broken_output[1]);
        if (!trace::Initialize(""))
            _exit(3);
        LOG("message to closed standard output");
        trace::Flush();
        bool stdout_open = fcntl(STDOUT_FILENO, F_GETFD) >= 0;
        trace::Drain();
        _exit(stdout_open ? 0 : 4);
    }
    close(broken_output[1]);
    int child_status;
    Check(waitpid(child, &child_status, 0) == child &&
                  WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0,
          "A logging failure closed standard output");

    unlink(backup.c_str());
    unlink(active.c_str());
    unlink(shutdown_log.c_str());
    rmdir(directory);
    return EXIT_SUCCESS;
}
