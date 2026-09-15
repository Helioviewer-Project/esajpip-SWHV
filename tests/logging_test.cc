#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

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

int main() {
    char directory[] = "/tmp/esajpip-log-XXXXXX";
    Check(mkdtemp(directory) != NULL, "Could not create logging test directory");
    string base = string(directory) + "/server";
    Check(TraceSystem::Initialize(base), "Could not initialize logging");

    LOG("first parent message");
    LOG("second parent message");
    TraceSystem::Drain();

    pid_t child = fork();
    Check(child >= 0, "Could not create logging test child");
    if (child == 0) {
        TraceSystem::CloseParentDescriptors();
        LOG("child message");
        _exit(0);
    }
    Check(waitpid(child, NULL, 0) == child, "Could not wait for logging test child");
    Check(TraceSystem::DrainOne(), "Could not write child log message");

    LOG(string(1100, 'x'));
    Check(TraceSystem::DrainOne(), "Could not rotate log");
    LOG("message after rollover");
    Check(TraceSystem::DrainOne(), "Could not write after log rollover");

    DIR *files = opendir(directory);
    Check(files != NULL, "Could not list logging test files");
    string active;
    for (dirent *entry = readdir(files); entry != NULL; entry = readdir(files)) {
        string name = entry->d_name;
        if (name.compare(0, 7, "server.") == 0 &&
            name.compare(name.size() - 4, 4, ".log") == 0) {
            active = string(directory) + "/" + name;
            break;
        }
    }
    closedir(files);
    Check(!active.empty(), "Active log was not created");
    string backup = active + ".1";

    string old_messages = ReadFile(backup);
    string new_messages = ReadFile(active);
    Check(old_messages.find("first parent message") != string::npos,
          "First parent message missing from rolled log");
    Check(old_messages.find("second parent message") != string::npos,
          "Second parent message missing from rolled log");
    Check(old_messages.find("child message") != string::npos,
          "Child message missing from rolled log");
    Check(new_messages.find("message after rollover") != string::npos,
          "Message missing after rollover");

    for (int i = 0; i < 10000; ++i)
        LOG(string(1800, 'x'));
    while (TraceSystem::DrainOne()) {
    }
    LOG("message after queue saturation");
    Check(TraceSystem::DrainOne(), "Dropped-message summary was not queued");
    Check(TraceSystem::DrainOne(), "Message was not queued after saturation");
    new_messages = ReadFile(active);
    Check(new_messages.find("log messages dropped") != string::npos,
          "Dropped log messages were not reported");
    Check(new_messages.find("message after queue saturation") != string::npos,
          "Logging did not recover after queue saturation");

    unlink(backup.c_str());
    Check(mkdir(backup.c_str(), 0700) == 0, "Could not obstruct log rollover");
    LOG(string(1100, 'x'));
    Check(!TraceSystem::DrainOne(), "Failed log rollover did not disable logging");

    int reused_fd = open("/dev/null", O_WRONLY);
    Check(reused_fd >= 0, "Could not reuse a descriptor after failed rollover");
    LOG("message after failed rollover");
    Check(TraceSystem::DrainOne(), "Could not discard a log after rollover failure");
    close(reused_fd);
    Check(rmdir(backup.c_str()) == 0, "Could not remove rollover obstruction");

    unlink(backup.c_str());
    unlink(active.c_str());
    rmdir(directory);
    return EXIT_SUCCESS;
}
