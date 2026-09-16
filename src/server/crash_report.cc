#include "server/crash_report.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <unistd.h>

using namespace std;

namespace {

thread_local atomic<uint64_t> channel(UINT64_MAX);
volatile sig_atomic_t report_descriptor = -1;

void Report(int signal_number) {
    uint64_t current_channel = channel.load(memory_order_relaxed);
    int fd = report_descriptor;
    if (fd >= 0) {
        ssize_t ignored = write(fd, &current_channel, sizeof current_channel);
        (void) ignored;
    }

    // SA_RESETHAND restored the default action. Queue the signal again so the
    // process still terminates normally for that fault, including a core dump
    // when the host enables one.
    if (kill(getpid(), signal_number) != 0)
        _exit(128 + signal_number);
}

}

namespace crash_report {

bool Initialize(int fd) {
    if (!channel.is_lock_free()) {
        errno = ENOTSUP;
        return false;
    }

    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = Report;
    action.sa_flags = SA_RESETHAND;
    sigemptyset(&action.sa_mask);

    const int signals[] = {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};
    for (int signal_number : signals) {
        if (sigaction(signal_number, &action, NULL) != 0)
            return false;
    }
    report_descriptor = fd;
    return true;
}

void SetChannel(uint64_t current_channel) {
    channel.store(current_channel, memory_order_relaxed);
}

}
