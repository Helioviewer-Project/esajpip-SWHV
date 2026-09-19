#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
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

#include <zlib.h>

#include "config.h"
#include "net/address.h"
#include "server/server.h"

using namespace std;

namespace {

using Clock = chrono::steady_clock;

const char HANDLED_HEADER[] =
        "JPIP-handled: tid,cid,cnew=http,stream,len,handled";

pid_t server_pid = -1;

void Fail(const char *message) {
    if (server_pid > 0) {
        kill(-server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
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
        size_t length = strtoul(response.headers.c_str() + position, NULL, 10);
        while (input->size() < length)
            Check(ReceiveMore(fd, input), "Connection closed inside HTTP body");
        response.body.assign(*input, 0, length);
        input->erase(0, length);
        return response;
    }

    for (;;) {
        size_t line_end;
        while ((line_end = input->find("\r\n")) == string::npos)
            Check(ReceiveMore(fd, input), "Connection closed inside chunk header");
        size_t length = strtoul(input->substr(0, line_end).c_str(), NULL, 16);
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
        result = inflate(&stream, Z_FINISH);
        plain.append(output, sizeof output - stream.avail_out);
    } while (result == Z_OK);
    inflateEnd(&stream);
    Check(result == Z_STREAM_END, "Server returned invalid gzip data");
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

string ReadLogs(const string &directory) {
    DIR *files = opendir(directory.c_str());
    Check(files != NULL, "Could not list server test logs");
    string contents;
    for (dirent *entry = readdir(files); entry != NULL; entry = readdir(files)) {
        string name = entry->d_name;
        if (name.compare(0, 6, "server") != 0 ||
            name.size() < 4 || name.compare(name.size() - 4, 4, ".log") != 0)
            continue;
        ifstream input((directory + "/" + name).c_str());
        contents.append(istreambuf_iterator<char>(input),
                        istreambuf_iterator<char>());
    }
    closedir(files);
    return contents;
}

pid_t StartServer(const Config &config, const string &log_name) {
    pid_t pid = fork();
    Check(pid >= 0, "Could not create the test server");
    if (pid == 0) {
        setpgid(0, 0);
        net::InetAddress address = config.address().empty()
                                           ? net::InetAddress(config.port())
                                           : net::InetAddress(
                                                     config.address().c_str(),
                                                     config.port());
        int result = RunServer(config, address, log_name,
                               "esajpip server test", 16);
        _exit(result == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    setpgid(pid, pid);
    server_pid = pid;
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
    if (server_pid == pid)
        server_pid = -1;
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
    Check(recv(fd, &byte, 1, 0) <= 0, message);
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

    uint16_t port = ReservePort();
    string config_text =
            "[listen]\nport = " + to_string(port) + "\naddress = 127.0.0.1\n"
            "[jpip]\nimage_directory = " + directory + "\nchunk_size = 128\n"
            "[connections]\ninitial_timeout = 1\ntimeout = 1\nlimit = 2\n"
            "[channels]\nlimit = 4\n"
            "[logging]\ndirectory =\nfile_enabled = false\nrequests = false\n";
    WriteFile(directory + "/server.ini", config_text.data(), config_text.size());
    Config config;
    string error;
    Check(config.Load((directory + "/server.ini").c_str(), error),
          "Could not load the server test configuration");

    server_pid = StartServer(config, directory + "/server");

    int unavailable = Connect(port);
    Check(unavailable >= 0, "Server did not start");
    SendRequest(unavailable,
                "/jpip?cid=ffffffffffffffffffffffffffffffff&tid=0&handled");
    Response unavailable_response = ReadResponse(unavailable);
    Check(unavailable_response.headers.find("503 Service Unavailable") != string::npos &&
                  unavailable_response.headers.find("JPIP-tid: 0") != string::npos &&
                  unavailable_response.headers.find(HANDLED_HEADER) != string::npos &&
                  unavailable_response.headers.find(
                      "Access-Control-Expose-Headers: JPIP-tid,JPIP-handled") !=
                      string::npos,
          "Unknown channel did not return 503 with JPIP response headers");
    Check(unavailable_response.body == "JPIP channel does not exist",
          "Unknown channel response did not explain the failure");
    CheckClosed(unavailable, 1000,
                "Unknown-channel response retained its connection");
    close(unavailable);

    pid_t primary_server = server_pid;
    pid_t conflicting_server = StartServer(config, directory + "/server-bind");
    int bind_status = WaitForServer(conflicting_server);
    Check(WIFEXITED(bind_status) && WEXITSTATUS(bind_status) != 0,
          "A second server bound the same endpoint");
    server_pid = primary_server;

    int bad_request = Connect(port);
    Check(bad_request >= 0, "Could not connect for the bad-request test");
    SendRequest(bad_request, "/image.jp2?cnew=http&fsiz=abc");
    Response bad_request_response = ReadResponse(bad_request);
    Check(bad_request_response.headers.find("400 Bad Request") != string::npos &&
                  bad_request_response.body == "Invalid JPIP fsiz parameter",
          "Bad JPIP request did not explain the invalid field");
    CheckClosed(bad_request, 1000,
                "Bad JPIP request retained its connection");
    close(bad_request);

    int request_body = Connect(port);
    Check(request_body >= 0, "Could not connect for request-body test");
    SendRequest(request_body, "/image.jp2?cnew=http", "Content-Length: 1\r\n");
    WriteAll(request_body, "x", 1);
    Response request_body_response = ReadResponse(request_body);
    Check(request_body_response.headers.find("400 Bad Request") != string::npos &&
                  request_body_response.body ==
                      "HTTP request bodies are not supported",
          "Body-bearing request did not return 400");
    CheckClosed(request_body, 1000,
                "Body-bearing request retained its connection");
    close(request_body);

    int missing_image = Connect(port);
    Check(missing_image >= 0, "Could not connect for the missing-image test");
    SendRequest(missing_image, "/missing.jp2?cnew=http");
    Response missing_image_response = ReadResponse(missing_image);
    Check(missing_image_response.headers.find("404 Not Found") != string::npos &&
                  missing_image_response.body ==
                      "The requested image was not found",
          "Missing image did not return 404 with an explanation");
    CheckClosed(missing_image, 1000,
                "Missing-image response retained its connection");
    close(missing_image);

    int bad_image = Connect(port);
    Check(bad_image >= 0, "Could not connect for the bad-image test");
    SendRequest(bad_image, "/image.jpeg?cnew=http&handled");
    Response bad_image_response = ReadResponse(bad_image);
    Check(bad_image_response.headers.find("404 Not Found") !=
                      string::npos &&
                  bad_image_response.headers.find(HANDLED_HEADER) !=
                      string::npos &&
                  bad_image_response.body ==
                      "The requested image type is not supported",
          "Image failure did not explain the unsupported type");
    CheckClosed(bad_image, 1000,
                "Invalid-image response retained its connection");
    close(bad_image);

    int invalid_path = Connect(port);
    Check(invalid_path >= 0, "Could not connect for the invalid-path test");
    SendRequest(invalid_path,
                "/image.jp2?cnew=http&target=../image.jp2");
    Response invalid_path_response = ReadResponse(invalid_path);
    Check(invalid_path_response.headers.find("404 Not Found") != string::npos &&
                  invalid_path_response.body ==
                      "The requested image path is invalid",
          "Invalid target path did not return 404 with an explanation");
    CheckClosed(invalid_path, 1000,
                "Invalid-path response retained its connection");
    close(invalid_path);

    int malformed_head = Connect(port);
    Check(malformed_head >= 0,
          "Could not connect for malformed request-head test");
    const string malformed_request =
            "GET /image.jp2?cnew=http HTTP/1.1\r\n\r\n";
    WriteAll(malformed_head, malformed_request.data(), malformed_request.size());
    Response malformed_response = ReadResponse(malformed_head);
    Check(malformed_response.headers.find("400 Bad Request") != string::npos &&
                  malformed_response.body == "Invalid HTTP request head",
          "Malformed identified request head did not return a useful 400");
    CheckClosed(malformed_head, 1000,
                "Malformed request head retained its connection");
    close(malformed_head);

    int unsupported_transport = Connect(port);
    Check(unsupported_transport >= 0,
          "Could not connect for unsupported transport test");
    SendRequest(unsupported_transport, "/image.jp2?cnew=http-tcp&len=128");
    Response unsupported_transport_response = ReadResponse(unsupported_transport);
    Check(unsupported_transport_response.headers.find("501 Not Implemented") !=
                      string::npos &&
                  unsupported_transport_response.headers.find("JPIP-cnew:") ==
                      string::npos &&
                  unsupported_transport_response.body ==
                      "The requested JPIP channel transport is not supported",
          "An unsupported channel transport did not return 501 without a channel");
    close(unsupported_transport);

    int fragmented = Connect(port);
    Check(fragmented >= 0, "Could not connect for fragmented request test");
    const string fragments[] = {
        "GET /image.jp2?cnew=http&type=jpp-stream&stream=0&",
        "fsiz=1,1&rsiz=1,1&roff=0,0&len=128 HTTP/1.1\r\n",
        "Host: local",
        "host\r\n\r\n"
    };
    for (const string &fragment : fragments)
        WriteAll(fragmented, fragment.data(), fragment.size());
    Response fragmented_response = ReadResponse(fragmented);
    Check(fragmented_response.headers.find("HTTP/1.1 200 OK") == 0 &&
                  !fragmented_response.body.empty(),
          "Fragmented request head was not served");
    string fragmented_channel = ChannelId(fragmented_response.headers);
    SendRequest(fragmented, "/jpip?cclose=" + fragmented_channel);
    Check(ReadResponse(fragmented).headers.find("HTTP/1.1 200 OK") == 0,
          "Fragmented-request channel could not be closed");
    CheckClosed(fragmented, 1000,
                "Fragmented-request channel retained its connection");
    close(fragmented);

    const string metadata_target =
            "/image.jp2?cnew=http&type=jpp-stream&stream=0&metareq=[*]!!&"
            "fsiz=1,1&rsiz=1,1&roff=0,0&len=128";
    int plain = Connect(port);
    Check(plain >= 0, "Could not connect for plain JPIP response test");
    SendRequest(plain, metadata_target);
    Response plain_response = ReadResponse(plain);
    Check(plain_response.headers.find("Content-Encoding: gzip") == string::npos &&
                  !plain_response.body.empty(),
          "Plain JPIP response was empty or encoded");
    string plain_channel = ChannelId(plain_response.headers);

    int compressed = Connect(port);
    Check(compressed >= 0, "Could not connect for gzip JPIP response test");
    SendRequest(compressed, metadata_target, "Accept-Encoding: gzip\r\n");
    Response compressed_response = ReadResponse(compressed);
    Check(compressed_response.headers.find("Content-Encoding: gzip") != string::npos,
          "Gzip JPIP response was not encoded");
    Check(Gunzip(compressed_response.body) == plain_response.body,
          "Plain and gzip JPIP responses differ after decompression");
    string compressed_channel = ChannelId(compressed_response.headers);

    SendRequest(plain, "/jpip?cclose=" + plain_channel);
    Check(ReadResponse(plain).headers.find("HTTP/1.1 200 OK") == 0,
          "Plain-response channel could not be closed");
    close(plain);
    SendRequest(compressed, "/jpip?cclose=" + compressed_channel);
    Check(ReadResponse(compressed).headers.find("HTTP/1.1 200 OK") == 0,
          "Gzip-response channel could not be closed");
    close(compressed);

    int modeled = Connect(port);
    Check(modeled >= 0, "Could not connect for cache-model test");
    SendRequest(modeled, "/image.jp2?cnew=http&len=3");
    Response model_created = ReadResponse(modeled);
    Check(model_created.headers.find("HTTP/1.1 200 OK") == 0 &&
                  model_created.body.size() == 3,
          "Could not create an initially empty cache model");
    string modeled_channel = ChannelId(model_created.headers);
    string modeled_target =
            "/jpip?cid=" + modeled_channel +
            "&stream=0&model=Hm:1&fsiz=1,1&rsiz=1,1&roff=0,0&len=512";
    SendRequest(modeled, modeled_target);
    Response partial_model = ReadResponse(modeled);
    Check(partial_model.headers.find("HTTP/1.1 200 OK") == 0 &&
                  partial_model.body.size() > 3,
          "Partial cache model did not produce the missing data");
    SendRequest(modeled,
                "/jpip?cid=" + modeled_channel +
                "&stream=0&fsiz=1,1&rsiz=1,1&roff=0,0&len=512");
    Response accumulated_model = ReadResponse(modeled);
    Check(accumulated_model.headers.find("HTTP/1.1 200 OK") == 0 &&
                  accumulated_model.body.size() == 3,
          "Cache-model state did not accumulate across responses");
    SendRequest(modeled, "/jpip?cclose=" + modeled_channel);
    Check(ReadResponse(modeled).headers.find("HTTP/1.1 200 OK") == 0,
          "Cache-model channel could not be closed");
    close(modeled);

    int duplicate = Connect(port);
    Check(duplicate >= 0, "Could not connect for duplicate channel creation");
    SendRequest(duplicate, "/image.jp2?cnew=http-tcp,http&len=128");
    Response selected_transport = ReadResponse(duplicate);
    Check(selected_transport.headers.find("HTTP/1.1 200 OK") == 0 &&
                  selected_transport.headers.find("transport=http") !=
                      string::npos,
          "HTTP was not selected from the offered transports");
    SendRequest(duplicate, "/image.jp2?cnew=http&len=128");
    Response duplicate_response = ReadResponse(duplicate);
    Check(duplicate_response.headers.find("HTTP/1.1 200 OK") == 0 &&
                  ChannelId(duplicate_response.headers) !=
                      ChannelId(selected_transport.headers),
          "A pooled connection could not create a second channel");
    close(duplicate);

    int channel = Connect(port);
    Check(channel >= 0, "Could not connect for channel creation");
    SendRequest(channel,
                "http://localhost/image.jp2?cnew=http&type=jpp-stream&stream=0&"
                "metareq=[*]!!&fsiz=1,1&rsiz=1,1&roff=0,0&len=128&handled",
                "Accept-Encoding: gzip\r\n");
    Response created = ReadResponse(channel);
    Check(created.headers.find("HTTP/1.1 200 OK") == 0 &&
                  created.headers.find(HANDLED_HEADER) != string::npos,
          "Channel creation did not return 200 with JPIP-handled");
    Check(created.headers.find("Content-Encoding: gzip") != string::npos,
          "Metadata response was not gzip encoded");
    Check(!Gunzip(created.body).empty(), "Gzip JPIP response was empty");
    string channel_id = ChannelId(created.headers);
    Check(channel_id.size() == 32 &&
                  channel_id.find_first_not_of("0123456789abcdef") ==
                      string::npos,
          "The server returned an invalid channel ID");

    int conflicting_close = Connect(port);
    Check(conflicting_close >= 0,
          "Could not connect for conflicting close test");
    SendRequest(conflicting_close,
                "/image.jp2?cnew=http&cclose=" + channel_id);
    Response conflicting_close_response = ReadResponse(conflicting_close);
    Check(conflicting_close_response.headers.find("400 Bad Request") !=
                      string::npos &&
                  conflicting_close_response.body ==
                      "JPIP cnew and cclose can not be combined",
          "Conflicting channel fields did not return 400");
    CheckClosed(conflicting_close, 1000,
                "Conflicting close retained its connection");
    close(conflicting_close);

    string pipelined =
            "GET /jpip?cid=" + channel_id +
            "&context=jpxl%3C0%3E&model=M0&fsiz=1,1&rsiz=1,1&roff=0,0&"
            "len=128&tid=0&handled "
            "HTTP/1.1\r\nHost: localhost\r\n\r\n"
            "GET /jpip?cid=" + channel_id +
            "&stream=0&fsiz=1,1&rsiz=1,1&roff=0,0&len=128 "
            "HTTP/1.1\r\nHost: localhost\r\n\r\n";
    WriteAll(channel, pipelined.data(), pipelined.size());
    string pipelined_input;
    Response first_pipelined = ReadResponse(channel, &pipelined_input);
    Response second_pipelined = ReadResponse(channel, &pipelined_input);
    Check(first_pipelined.headers.find("HTTP/1.1 200 OK") == 0 &&
                  first_pipelined.headers.find("JPIP-tid: 0") != string::npos &&
                  first_pipelined.headers.find(HANDLED_HEADER) != string::npos &&
                  !first_pipelined.body.empty(),
          "First pipelined request was not served correctly");
    Check(second_pipelined.headers.find("HTTP/1.1 200 OK") == 0 &&
                  second_pipelined.headers.find("JPIP-tid: 0") == string::npos &&
                  second_pipelined.headers.find("JPIP-handled:") == string::npos &&
                  !second_pipelined.body.empty(),
          "Pipelined responses were not returned in request order");

    string partial = "GET /jpip?cid=" + channel_id;
    WriteAll(channel, partial.data(), partial.size());
    int replacement = Connect(port);
    Check(replacement >= 0, "Could not create a replacement connection");
    SendRequest(replacement,
                "/jpip?cid=" + channel_id +
                "&stream=0&fsiz=1,1&rsiz=1,1&roff=0,0&len=128&tid=0&handled");
    Response replaced = ReadResponse(replacement);
    Check(replaced.headers.find("HTTP/1.1 200 OK") == 0 &&
                  replaced.headers.find("JPIP-tid: 0") != string::npos &&
                  replaced.headers.find(HANDLED_HEADER) != string::npos &&
                  !replaced.body.empty(),
          "Replacement connection did not continue the channel");
    SendRequest(replacement,
                "/jpip?cid=" + channel_id +
                "&stream=0&fsiz=1,1&rsiz=1,1&roff=0,0&len=128",
                "Connection: close\r\n");
    Response close_response = ReadResponse(replacement);
    Check(close_response.headers.find("HTTP/1.1 200 OK") == 0 &&
                  close_response.headers.find("Connection: close") !=
                          string::npos,
          "Connection close request was not served with a close header");
    CheckClosed(replacement, 1000,
                "Connection close request retained its connection");
    close(replacement);

    replacement = Connect(port);
    Check(replacement >= 0,
          "Could not reconnect after Connection close");
    SendRequest(replacement,
                "/jpip?cid=" + channel_id +
                "&stream=0&fsiz=1,1&rsiz=1,1&roff=0,0&len=128");
    Check(ReadResponse(replacement).headers.find("HTTP/1.1 200 OK") == 0,
          "Channel was lost after Connection close");

    SendRequest(replacement,
                "/jpip?cclose=" + channel_id + "&tid=0&handled");
    Response closed = ReadResponse(replacement);
    Check(closed.headers.find("HTTP/1.1 200 OK") == 0 &&
                  closed.headers.find("JPIP-tid: 0") != string::npos &&
                  closed.headers.find(HANDLED_HEADER) != string::npos &&
                  closed.headers.find("Connection: close") != string::npos,
          "Channel close did not return 200 with JPIP capability headers");
    CheckClosed(replacement, 1000, "Closed channel retained its connection");
    close(replacement);
    CheckClosed(channel, 2500, "Abandoned partial connection did not expire");
    close(channel);

    int oversized = Connect(port);
    Check(oversized >= 0, "Could not connect for the request-limit test");
    SendRequest(oversized,
                "/image.jp2?cnew=http&type=jpp-stream&stream=0&"
                "fsiz=1,1&rsiz=1,1&roff=0,0&len=128");
    Response oversized_created = ReadResponse(oversized);
    Check(oversized_created.headers.find("HTTP/1.1 200 OK") == 0,
          "Request-limit channel creation failed");
    string oversized_channel = ChannelId(oversized_created.headers);
    SendRequest(oversized, "/jpip?cid=" + oversized_channel,
                "X-Large: " + string(4096, 'x') + "\r\n");
    Response oversized_response = ReadResponse(oversized);
    Check(oversized_response.headers.find(
                  "431 Request Header Fields Too Large") != string::npos &&
                  oversized_response.body == "HTTP request head is too large",
          "Oversized request head did not return 431");
    CheckClosed(oversized, 1000, "Oversized request retained its channel");
    close(oversized);
    int oversized_retry = Connect(port);
    Check(oversized_retry >= 0,
          "Could not reconnect after the oversized request");
    SendRequest(oversized_retry, "/jpip?cid=" + oversized_channel + "&len=128");
    Check(ReadResponse(oversized_retry).headers.find(
                  "503 Service Unavailable") != string::npos,
          "Oversized request did not end its channel");
    close(oversized_retry);

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
    string limited_ids[2];
    for (int i = 0; i < 2; ++i) {
        int &connection = limited[i];
        connection = Connect(port);
        Check(connection >= 0, "Could not fill the connection limit");
        SendRequest(connection,
                    "/image.jp2?cnew=http&type=jpp-stream&stream=0&"
                    "fsiz=1,1&rsiz=1,1&roff=0,0&len=128");
        Response response = ReadResponse(connection);
        Check(response.headers.find("HTTP/1.1 200 OK") == 0,
              "Connection-limit channel creation failed");
        limited_ids[i] = ChannelId(response.headers);
    }
    Check(limited_ids[0] != limited_ids[1],
          "Two channels received the same random ID");
    int refused = Connect(port);
    Check(refused >= 0, "Could not connect for the connection-limit test");
    CheckClosed(refused, 1000, "Connection limit did not reject a client");
    close(refused);
    close(limited[0]);
    close(limited[1]);

    Check(kill(-primary_server, SIGTERM) == 0,
          "Could not stop the server process group");
    int status = WaitForServer(primary_server);
    Check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "SIGTERM did not stop the server cleanly");

    uint16_t channel_limit_port = ReservePort();
    string channel_limit_text =
            "[listen]\nport = " + to_string(channel_limit_port) +
            "\naddress = 127.0.0.1\n"
            "[jpip]\nimage_directory = " + directory + "\nchunk_size = 128\n"
            "[connections]\ninitial_timeout = 1\ntimeout = 1\nlimit = 1\n"
            "[channels]\nlimit = 2\n"
            "[logging]\ndirectory =\nfile_enabled = false\nrequests = false\n";
    WriteFile(directory + "/channel-limit.ini", channel_limit_text.data(),
              channel_limit_text.size());
    Config channel_limit_config;
    Check(channel_limit_config.Load((directory + "/channel-limit.ini").c_str(),
                                    error),
          "Could not load the channel-limit configuration");
    pid_t channel_limit_server = StartServer(
            channel_limit_config, directory + "/server-channel-limit");
    int pooled = Connect(channel_limit_port);
    Check(pooled >= 0, "Channel-limit server did not start");
    SendRequest(pooled, "/image.jp2?cnew=http&len=128");
    Check(ReadResponse(pooled).headers.find("HTTP/1.1 200 OK") == 0,
          "First pooled channel was not created");
    SendRequest(pooled, "/image.jp2?cnew=http&len=128");
    Check(ReadResponse(pooled).headers.find("HTTP/1.1 200 OK") == 0,
          "Second pooled channel was not created");
    SendRequest(pooled, "/image.jp2?cnew=http&len=128");
    Response channel_limit_response = ReadResponse(pooled);
    Check(channel_limit_response.headers.find("503 Service Unavailable") !=
                      string::npos &&
                  channel_limit_response.body ==
                      "JPIP channel limit has been reached",
          "The independent channel limit was not enforced");
    CheckClosed(pooled, 1000,
                "Channel-limit response retained its connection");
    close(pooled);
    Check(kill(channel_limit_server, SIGINT) == 0,
          "Could not stop the channel-limit server");
    status = WaitForServer(channel_limit_server);
    Check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "SIGINT did not stop the channel-limit server cleanly");

    pid_t failing_server = StartServer(config, directory + "/missing/server");
    status = WaitForServer(failing_server);
    Check(WIFEXITED(status) && WEXITSTATUS(status) != 0,
          "Server startup failure did not return an error");

    string logs = ReadLogs(directory);
    Check(logs.find("Server stopping") != string::npos,
          "Orderly server shutdown was not recorded");

    RemoveDirectory(directory);
    return EXIT_SUCCESS;
}
