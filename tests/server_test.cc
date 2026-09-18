#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <zlib.h>

#include "config.h"
#include "server/supervisor.h"

using namespace std;

namespace {

using Clock = chrono::steady_clock;

pid_t supervisor_pid = -1;

void Fail(const char *message) {
    if (supervisor_pid > 0) {
        kill(-supervisor_pid, SIGKILL);
        waitpid(supervisor_pid, NULL, 0);
    }
    cerr << message << endl;
    exit(EXIT_FAILURE);
}

void Check(bool condition, const char *message) {
    if (!condition)
        Fail(message);
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

int CreateListenSocket(uint16_t *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Check(fd >= 0, "Could not create the server test listen socket");
    int enabled = 1;
    Check(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled) == 0,
          "Could not configure the server test listen socket");

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Check(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0,
          "Could not bind the server test listen socket");
    Check(listen(fd, 10) == 0, "Could not listen on the server test socket");
    socklen_t length = sizeof address;
    Check(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) == 0,
          "Could not read the server test port");
    *port = ntohs(address.sin_port);

    int flags = fcntl(fd, F_GETFL);
    Check(flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0,
          "Could not make the server test listen socket nonblocking");
    return fd;
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

Response ReadResponse(int fd) {
    string input;
    size_t end;
    while ((end = input.find("\r\n\r\n")) == string::npos)
        Check(ReceiveMore(fd, &input), "Connection closed before response headers");

    Response response;
    response.headers = input.substr(0, end);
    input.erase(0, end + 4);
    if (response.headers.find("Transfer-Encoding: chunked") == string::npos) {
        size_t position = response.headers.find("Content-Length: ");
        Check(position != string::npos, "HTTP response has no body framing");
        position += strlen("Content-Length: ");
        size_t length = strtoul(response.headers.c_str() + position, NULL, 10);
        while (input.size() < length)
            Check(ReceiveMore(fd, &input), "Connection closed inside HTTP body");
        response.body.assign(input, 0, length);
        return response;
    }

    for (;;) {
        size_t line_end;
        while ((line_end = input.find("\r\n")) == string::npos)
            Check(ReceiveMore(fd, &input), "Connection closed inside chunk header");
        size_t length = strtoul(input.substr(0, line_end).c_str(), NULL, 16);
        input.erase(0, line_end + 2);
        while (input.size() < length + 2)
            Check(ReceiveMore(fd, &input), "Connection closed inside HTTP chunk");
        Check(input.compare(length, 2, "\r\n") == 0,
              "HTTP chunk has no terminator");
        response.body.append(input, 0, length);
        input.erase(0, length + 2);
        if (length == 0)
            return response;
    }
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
        result = inflate(&stream, Z_FINISH);
        plain.append(output, sizeof output - stream.avail_out);
    } while (result == Z_OK);
    inflateEnd(&stream);
    Check(result == Z_STREAM_END, "Server returned invalid gzip data");
    return plain;
}

uint64_t ChannelId(const string &headers) {
    const string prefix = "JPIP-cnew: cid=";
    size_t position = headers.find(prefix);
    Check(position != string::npos, "Channel response has no JPIP-cnew header");
    position += prefix.size();
    char *end;
    uint64_t id = strtoull(headers.c_str() + position, &end, 10);
    Check(end != headers.c_str() + position && *end == ',',
          "Channel response has an invalid channel ID");
    return id;
}

string ReadLogs(const string &directory) {
    DIR *files = opendir(directory.c_str());
    Check(files != NULL, "Could not list server test logs");
    string contents;
    for (dirent *entry = readdir(files); entry != NULL; entry = readdir(files)) {
        string name = entry->d_name;
        if (name.compare(0, 7, "server.") != 0 ||
            name.size() < 4 || name.compare(name.size() - 4, 4, ".log") != 0)
            continue;
        ifstream input((directory + "/" + name).c_str());
        contents.append(istreambuf_iterator<char>(input),
                        istreambuf_iterator<char>());
    }
    closedir(files);
    return contents;
}

pid_t FindServingPid(const string &logs, pid_t previous) {
    const string prefix = "Serving process created (PID = ";
    size_t position = 0;
    pid_t found = -1;
    while ((position = logs.find(prefix, position)) != string::npos) {
        position += prefix.size();
        pid_t pid = static_cast<pid_t>(
                strtol(logs.c_str() + position, NULL, 10));
        if (pid != previous)
            found = pid;
    }
    return found;
}

pid_t WaitForServingPid(const string &directory, pid_t previous = -1) {
    Clock::time_point deadline = Clock::now() + chrono::seconds(5);
    do {
        pid_t pid = FindServingPid(ReadLogs(directory), previous);
        if (pid > 0)
            return pid;
        this_thread::sleep_for(chrono::milliseconds(20));
    } while (Clock::now() < deadline);
    return -1;
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
    Check(recv(fd, &byte, 1, 0) == 0, message);
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

}

int main() {
    char directory_template[] = "/tmp/esajpip-server-XXXXXX";
    char *directory_name = mkdtemp(directory_template);
    Check(directory_name != NULL, "Could not create the server test directory");
    string directory = directory_name;

    static const unsigned char image[] = {
        0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A,
        0x00, 0x00, 0x00, 0x14, 0x66, 0x74, 0x79, 0x70, 0x6A, 0x70, 0x32, 0x20,
        0x00, 0x00, 0x00, 0x00, 0x6A, 0x70, 0x32, 0x20, 0x00, 0x00, 0x00, 0x5F,
        0x6A, 0x70, 0x32, 0x63, 0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x07, 0x01,
        0x01, 0xFF, 0x52, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0xFF, 0x5C, 0x00, 0x03, 0x00, 0xFF, 0x90, 0x00, 0x0A,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x01, 0xFF, 0x58, 0x00, 0x04,
        0x00, 0x01, 0xFF, 0x93, 0x00, 0xFF, 0xD9
    };
    WriteFile(directory + "/image.jp2", image, sizeof image);

    uint16_t port;
    int listen_socket = CreateListenSocket(&port);
    string config_text =
            "[listen]\nport = " + to_string(port) + "\naddress = 127.0.0.1\n"
            "[jpip]\nimage_directory = " + directory + "\nchunk_size = 128\n"
            "[connections]\ninitial_timeout = 1\ntimeout = 1\nlimit = 2\n"
            "[logging]\ndirectory =\nfile_enabled = 0\nrequests = 0\n";
    WriteFile(directory + "/server.ini", config_text.data(), config_text.size());
    Config config;
    string error;
    Check(config.Load((directory + "/server.ini").c_str(), error),
          "Could not load the server test configuration");

    supervisor_pid = fork();
    Check(supervisor_pid >= 0, "Could not create the server test supervisor");
    if (supervisor_pid == 0) {
        setpgid(0, 0);
        int result = RunSupervisor(config, listen_socket, directory + "/server",
                                   "esajpip server test");
        _exit(result == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    setpgid(supervisor_pid, supervisor_pid);
    close(listen_socket);

    int unavailable = Connect(port);
    Check(unavailable >= 0, "Serving process did not start");
    SendRequest(unavailable, "/jpip?cid=999");
    Response unavailable_response = ReadResponse(unavailable);
    Check(unavailable_response.headers.find("503 Service Unavailable") != string::npos,
          "Unknown channel did not return 503");
    Check(unavailable_response.body == "JPIP channel does not exist",
          "Unknown channel response did not explain the failure");
    close(unavailable);

    pid_t first_server = WaitForServingPid(directory);
    Check(first_server > 0, "Serving process was not recorded in the log");

    int bad_request = Connect(port);
    Check(bad_request >= 0, "Could not connect for the bad-request test");
    SendRequest(bad_request, "/image.jp2?cnew=http&fsiz=abc");
    Response bad_request_response = ReadResponse(bad_request);
    Check(bad_request_response.headers.find("400 Bad Request") != string::npos &&
                  bad_request_response.body == "Invalid JPIP fsiz parameter",
          "Bad JPIP request did not explain the invalid field");
    close(bad_request);

    int bad_image = Connect(port);
    Check(bad_image >= 0, "Could not connect for the bad-image test");
    SendRequest(bad_image, "/image.jpeg?cnew=http");
    Response bad_image_response = ReadResponse(bad_image);
    Check(bad_image_response.headers.find("500 Internal Server Error") !=
                      string::npos &&
                  bad_image_response.body ==
                      "The requested image type is not supported",
          "Image failure did not explain the unsupported type");
    close(bad_image);

    int duplicate = Connect(port);
    Check(duplicate >= 0, "Could not connect for duplicate channel creation");
    SendRequest(duplicate, "/image.jp2?cnew=http&len=128");
    Check(ReadResponse(duplicate).headers.find("HTTP/1.1 200 OK") == 0,
          "Initial duplicate-test channel creation failed");
    SendRequest(duplicate, "/image.jp2?cnew=http&len=128");
    Response duplicate_response = ReadResponse(duplicate);
    Check(duplicate_response.headers.find("503 Service Unavailable") !=
                      string::npos &&
                  duplicate_response.body ==
                      "A JPIP channel is already open on this connection",
          "A second channel on one connection did not return 503");
    CheckClosed(duplicate, 1000,
                "Duplicate channel request retained its connection");
    close(duplicate);

    int channel = Connect(port);
    Check(channel >= 0, "Could not connect for channel creation");
    SendRequest(channel,
                "/image.jp2?cnew=http&type=jpp-stream&stream=0&metareq=[*]!!&"
                "fsiz=1,1&rsiz=1,1&roff=0,0&len=128",
                "Accept-Encoding: gzip\r\n");
    Response created = ReadResponse(channel);
    Check(created.headers.find("HTTP/1.1 200 OK") == 0,
          "Channel creation did not return 200");
    Check(created.headers.find("Content-Encoding: gzip") != string::npos,
          "Metadata response was not gzip encoded");
    Check(!Gunzip(created.body).empty(), "Gzip JPIP response was empty");
    uint64_t channel_id = ChannelId(created.headers);

    string partial = "GET /jpip?cid=" + to_string(channel_id);
    WriteAll(channel, partial.data(), partial.size());
    int replacement = Connect(port);
    Check(replacement >= 0, "Could not create a replacement connection");
    SendRequest(replacement,
                "/jpip?cid=" + to_string(channel_id) +
                "&stream=0&fsiz=1,1&rsiz=1,1&roff=0,0&len=128");
    Response replaced = ReadResponse(replacement);
    Check(replaced.headers.find("HTTP/1.1 200 OK") == 0 && !replaced.body.empty(),
          "Replacement connection did not continue the channel");
    CheckClosed(channel, 1000, "Replaced connection remained open");
    close(channel);

    SendRequest(replacement, "/jpip?cclose=" + to_string(channel_id));
    Response closed = ReadResponse(replacement);
    Check(closed.headers.find("HTTP/1.1 200 OK") == 0,
          "Channel close did not return 200");
    CheckClosed(replacement, 1000, "Closed channel retained its connection");
    close(replacement);

    int oversized = Connect(port);
    Check(oversized >= 0, "Could not connect for the request-limit test");
    SendRequest(oversized,
                "/image.jp2?cnew=http&type=jpp-stream&stream=0&"
                "fsiz=1,1&rsiz=1,1&roff=0,0&len=128");
    Response oversized_created = ReadResponse(oversized);
    Check(oversized_created.headers.find("HTTP/1.1 200 OK") == 0,
          "Request-limit channel creation failed");
    uint64_t oversized_channel = ChannelId(oversized_created.headers);
    SendRequest(oversized, "/jpip?cid=" + to_string(oversized_channel),
                "X-Large: " + string(4096, 'x') + "\r\n");
    Response oversized_response = ReadResponse(oversized);
    Check(oversized_response.headers.find(
                  "431 Request Header Fields Too Large") != string::npos &&
                  oversized_response.body == "HTTP request head is too large",
          "Oversized request head did not return 431");
    CheckClosed(oversized, 1000, "Oversized request retained its channel");
    close(oversized);

    int idle = Connect(port);
    Check(idle >= 0, "Could not connect for the timeout test");
    SendRequest(idle,
                "/image.jp2?cnew=http&type=jpp-stream&stream=0&"
                "fsiz=1,1&rsiz=1,1&roff=0,0&len=128");
    Check(ReadResponse(idle).headers.find("HTTP/1.1 200 OK") == 0,
          "Timeout-test channel creation failed");
    CheckClosed(idle, 2000, "Idle channel did not time out");
    close(idle);

    int expiring = Connect(port);
    Check(expiring >= 0, "Could not connect for the admission-timeout test");
    CheckClosed(expiring, 2000, "Unidentified connection did not expire");
    close(expiring);

    int limited[2];
    for (int &connection : limited) {
        connection = Connect(port);
        Check(connection >= 0, "Could not fill the connection limit");
        SendRequest(connection,
                    "/image.jp2?cnew=http&type=jpp-stream&stream=0&"
                    "fsiz=1,1&rsiz=1,1&roff=0,0&len=128");
        Check(ReadResponse(connection).headers.find("HTTP/1.1 200 OK") == 0,
              "Connection-limit channel creation failed");
    }
    int refused = Connect(port);
    Check(refused >= 0, "Could not connect for the connection-limit test");
    CheckClosed(refused, 1000, "Connection limit did not reject a client");
    close(refused);
    close(limited[0]);
    close(limited[1]);

    Check(kill(first_server, SIGKILL) == 0, "Could not kill the serving process");
    pid_t replacement_server = WaitForServingPid(directory, first_server);
    Check(replacement_server > 0, "Supervisor did not restart the serving process");
    int after_restart = Connect(port);
    Check(after_restart >= 0, "Restarted serving process did not accept connections");
    SendRequest(after_restart, "/jpip?cid=999");
    Check(ReadResponse(after_restart).headers.find("503 Service Unavailable") != string::npos,
          "Restarted serving process did not handle a request");
    close(after_restart);

    Check(kill(supervisor_pid, SIGTERM) == 0, "Could not stop the supervisor");
    int status;
    Check(waitpid(supervisor_pid, &status, 0) == supervisor_pid,
          "Could not wait for the supervisor");
    supervisor_pid = -1;
    Check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "Supervisor did not stop cleanly");

    string logs = ReadLogs(directory);
    Check(logs.find("The serving process was killed; restarting after a short delay") !=
                  string::npos,
          "Serving-process restart was not recorded");
    Check(logs.find("Serving process stopping") != string::npos,
          "Orderly serving-process shutdown was not recorded");

    RemoveDirectory(directory);
    return EXIT_SUCCESS;
}
