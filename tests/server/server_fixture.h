#pragma once

// Loopback server harness and independent HTTP/JPP decoders.
// Included once by each live-server test executable.
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <thread>
#include <unistd.h>
#include <vector>

#include <zlib.h>

#include "server/config.h"
#include "server/server.h"

#ifdef ESAJPIP_COVERAGE
extern "C" void __llvm_profile_set_filename(const char *name);
extern "C" void __llvm_profile_reset_counters(void);
extern "C" int __llvm_profile_write_file(void);
#endif

using namespace std;

namespace server_test {

using Clock = chrono::steady_clock;

const char HANDLED_HEADER[] =
        "JPIP-handled: tid,cid,cnew=http,stream,len,handled";

// The bind-conflict case has two live servers; all other cases have one.
volatile sig_atomic_t child_pids[2] = {};
string test_directory;
string test_case;

void Deadline(int) {
    for (sig_atomic_t child : child_pids)
        if (child > 0) kill(-static_cast<pid_t>(child), SIGKILL);
    static const char message[] = "Live-server test Timeout: exceeded its deadline\n";
    (void) write(STDERR_FILENO, message, sizeof message - 1);
    _exit(EXIT_FAILURE);
}

void Fail(const char *message) {
    for (volatile sig_atomic_t &child : child_pids) {
        if (child <= 0) continue;
        kill(-static_cast<pid_t>(child), SIGKILL);
        while (waitpid(static_cast<pid_t>(child), NULL, 0) < 0 && errno == EINTR) {}
        child = 0;
    }
    if (!test_case.empty()) cerr << test_case << ": ";
    cerr << message << endl;
    if (!test_directory.empty()) cerr << "Test files and logs: " << test_directory << endl;
    exit(EXIT_FAILURE);
}

void Check(bool condition, const char *message) {
    if (!condition)
        Fail(message);
}

void InitializeHarness() {
    Check(signal(SIGPIPE, SIG_IGN) != SIG_ERR && signal(SIGALRM, Deadline) != SIG_ERR,
          "Could not install live-test signal handlers");
    // Exit before CTest's hard timeout, which cannot reap our process groups.
    double seconds = ESAJPIP_LIVE_TEST_TIMEOUT * 0.9;
    itimerval timer = {};
    timer.it_value.tv_sec = static_cast<time_t>(seconds);
    timer.it_value.tv_usec = static_cast<suseconds_t>((seconds - timer.it_value.tv_sec) * 1000000);
    Check(setitimer(ITIMER_REAL, &timer, NULL) == 0, "Could not start live-test deadline");
}

void WriteAll(int fd, const void *data, size_t length) {
    const char *position = static_cast<const char *>(data);
    while (length > 0) {
        ssize_t written = write(fd, position, length);
        if (written < 0 && errno == EINTR)
            continue;
        Check(written > 0, "Could not write test data");
        position += written;
        length -= written;
    }
}

void WriteFile(const string &path, const void *data, size_t length) {
    int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    Check(fd >= 0, "Could not create a server test file");
    WriteAll(fd, data, length);
    close(fd);
}

uint16_t ReservePort() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Check(fd >= 0, "Could not create a server test socket");

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Check(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0,
          "Could not reserve a server test port");
    socklen_t length = sizeof address;
    Check(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) == 0,
          "Could not read the server test port");
    uint16_t port = ntohs(address.sin_port);
    close(fd);
    return port;
}

int Connect(uint16_t port, int timeout_ms = 5000) {
    Clock::time_point deadline = Clock::now() + chrono::milliseconds(timeout_ms);
    for (;;) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        Check(fd >= 0, "Could not create a server test client socket");
        timeval timeout = {5, 0};
        Check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0,
              "Could not configure the server test client socket");

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0)
            return fd;
        int error = errno;
        close(fd);
        if (Clock::now() >= deadline)
            return -1;
        if (error != ECONNREFUSED && error != EINTR)
            return -1;
        this_thread::sleep_for(chrono::milliseconds(20));
    }
}

void SendRequest(int fd, const string &target, const string &headers = "") {
    string request = "GET " + target + " HTTP/1.1\r\nHost: localhost\r\n" +
                     headers + "\r\n";
    WriteAll(fd, request.data(), request.size());
}

bool ReceiveMore(int fd, string *buffer) {
    char data[4096];
    ssize_t received;
    do {
        received = recv(fd, data, sizeof data, 0);
    } while (received < 0 && errno == EINTR);
    if (received <= 0)
        return false;
    buffer->append(data, received);
    return true;
}

struct Response {
    string headers;
    string body;
};

size_t ResponseLength(const string &text, int base) {
    const char *digits = base == 16 ? "0123456789abcdefABCDEF" : "0123456789";
    Check(!text.empty() && text.find_first_not_of(digits) == string::npos,
          "Invalid HTTP response length");
    errno = 0;
    unsigned long long length = strtoull(text.c_str(), NULL, base);
    Check(errno == 0 && length <= numeric_limits<size_t>::max() - 2,
          "Overflowing HTTP response length");
    return static_cast<size_t>(length);
}

Response ReadResponse(int fd, string *input) {
    size_t end;
    while ((end = input->find("\r\n\r\n")) == string::npos)
        Check(ReceiveMore(fd, input), "Connection closed before response headers");

    Response response;
    response.headers = input->substr(0, end);
    input->erase(0, end + 4);
    if (response.headers.find("Transfer-Encoding: chunked") == string::npos) {
        size_t position = response.headers.find("Content-Length: ");
        Check(position != string::npos, "HTTP response has no body framing");
        position += strlen("Content-Length: ");
        size_t end = response.headers.find("\r\n", position);
        size_t length = ResponseLength(response.headers.substr(position, end - position), 10);
        while (input->size() < length)
            Check(ReceiveMore(fd, input), "Connection closed inside HTTP body");
        response.body.assign(*input, 0, length);
        input->erase(0, length);
        return response;
    }

    Check(response.headers.find("Content-Length:") == string::npos,
          "Chunked response also carries Content-Length");

    for (;;) {
        size_t line_end;
        while ((line_end = input->find("\r\n")) == string::npos)
            Check(ReceiveMore(fd, input), "Connection closed inside chunk header");
        size_t length = ResponseLength(input->substr(0, line_end), 16);
        input->erase(0, line_end + 2);
        while (input->size() < length + 2)
            Check(ReceiveMore(fd, input), "Connection closed inside HTTP chunk");
        Check(input->compare(length, 2, "\r\n") == 0,
              "HTTP chunk has no terminator");
        response.body.append(*input, 0, length);
        input->erase(0, length + 2);
        if (length == 0)
            return response;
    }
}

Response ReadResponse(int fd) {
    string input;
    return ReadResponse(fd, &input);
}

string Gunzip(const string &compressed) {
    z_stream stream = {};
    Check(inflateInit2(&stream, MAX_WBITS + 16) == Z_OK,
          "Could not initialize gzip decoding");
    stream.next_in = reinterpret_cast<Bytef *>(
            const_cast<char *>(compressed.data()));
    stream.avail_in = compressed.size();
    string plain;
    char output[4096];
    int result;
    do {
        stream.next_out = reinterpret_cast<Bytef *>(output);
        stream.avail_out = sizeof output;
        result = inflate(&stream, Z_NO_FLUSH);
        plain.append(output, sizeof output - stream.avail_out);
    } while (result == Z_OK);
    bool complete = result == Z_STREAM_END && stream.avail_in == 0;
    inflateEnd(&stream);
    Check(complete, "Server returned truncated gzip data or bytes after the gzip stream");
    return plain;
}

string ChannelId(const string &headers) {
    const string prefix = "JPIP-cnew: cid=";
    size_t position = headers.find(prefix);
    Check(position != string::npos, "Channel response has no JPIP-cnew header");
    position += prefix.size();
    size_t end = headers.find(',', position);
    Check(end != string::npos,
          "Channel response has an invalid channel ID");
    string id = headers.substr(position, end - position);
    Check(id.size() == 32, "Channel response has a wrongly sized channel ID");
    for (char c : id)
        Check((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'),
              "Channel response has a non-canonical channel ID");
    return id;
}

string ReadLogs(const string &directory, const string &prefix = "server") {
    DIR *files = opendir(directory.c_str());
    Check(files != NULL, "Could not list server test logs");
    string contents;
    for (dirent *entry = readdir(files); entry != NULL; entry = readdir(files)) {
        string name = entry->d_name;
        if (name.compare(0, prefix.size(), prefix) != 0 ||
            name.size() < 4 || name.compare(name.size() - 4, 4, ".log") != 0)
            continue;
        ifstream input((directory + "/" + name).c_str());
        contents.append(istreambuf_iterator<char>(input),
                        istreambuf_iterator<char>());
    }
    closedir(files);
    return contents;
}

void WaitForLog(const string &directory, const string &message,
                const string &prefix = "server") {
    Clock::time_point deadline = Clock::now() + chrono::seconds(5);
    do {
        if (ReadLogs(directory, prefix).find(message) != string::npos) return;
        this_thread::sleep_for(chrono::milliseconds(1));
    } while (Clock::now() < deadline);
    Fail("Expected server log record did not appear before the deadline");
}

// The server opens half as many images at once as it has I/O threads.
pid_t StartServer(const server::Config &config, const string &log_name,
                  unsigned int io_threads = 16) {
    pid_t pid = fork();
    Check(pid >= 0, "Could not create the test server");
    if (pid == 0) {
#ifdef ESAJPIP_COVERAGE
        // LLVM caches %p and ignores an unchanged pattern. A different
        // basename forces expansion with this child's PID.
        const char *profile_pattern = getenv("LLVM_PROFILE_FILE");
        string child_profile = profile_pattern ? profile_pattern : "%p.profraw";
        size_t slash = child_profile.find_last_of('/');
        child_profile.insert(slash == string::npos ? 0 : slash + 1, "child-");
        __llvm_profile_set_filename(child_profile.c_str());
        // Parent counters are collected by the parent process.
        __llvm_profile_reset_counters();
#endif
        setpgid(0, 0);
        int result = server::RunServer(config, log_name, "esajpip server test",
                                       io_threads, io_threads / 2);
#ifdef ESAJPIP_COVERAGE
        // _exit skips the profiling runtime's normal exit handler.
        if (__llvm_profile_write_file() != 0) {
            cerr << "Could not write server coverage profile" << endl;
            result = 1;
        }
#endif
        _exit(result == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    setpgid(pid, pid);
    bool tracked = false;
    for (volatile sig_atomic_t &child : child_pids) {
        if (child != 0) continue;
        child = pid;
        tracked = true;
        break;
    }
    if (!tracked) { kill(-pid, SIGKILL); waitpid(pid, NULL, 0); }
    Check(tracked, "Live-test child tracking is full");
    return pid;
}

int WaitForServer(pid_t pid) {
    int status;
    Clock::time_point deadline = Clock::now() + chrono::seconds(5);
    pid_t result;
    do {
        result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            break;
        Check(result == 0 || (result < 0 && errno == EINTR),
              "Could not wait for the server");
        this_thread::sleep_for(chrono::milliseconds(20));
    } while (Clock::now() < deadline);
    Check(result == pid, "Server did not exit before the deadline");
    for (volatile sig_atomic_t &child : child_pids)
        if (child == pid) child = 0;
    return status;
}

void CheckClosed(int fd, int timeout_ms, const char *message) {
    pollfd event = {fd, POLLIN | POLLHUP, 0};
    int result;
    do {
        result = poll(&event, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result <= 0)
        Fail(message);
    char byte;
    ssize_t received;
    do { received = recv(fd, &byte, 1, 0); } while (received < 0 && errno == EINTR);
    Check(received == 0 || (received < 0 && errno == ECONNRESET), message);
}

void RemoveDirectory(const string &directory) {
    DIR *files = opendir(directory.c_str());
    if (files) {
        for (dirent *entry = readdir(files); entry != NULL; entry = readdir(files)) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            unlink((directory + "/" + entry->d_name).c_str());
        }
        closedir(files);
    }
    rmdir(directory.c_str());
}

#include "jpp_validation.h"

}

