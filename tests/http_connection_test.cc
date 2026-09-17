#include <sys/socket.h>
#include <sys/time.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

#include "http/connection.h"
#include "server/connection_queue.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static void CreateSocketPair(int sockets[2], const char *message) {
    Check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, message);
    timeval timeout = {5, 0};
    for (int i = 0; i < 2; ++i) {
        int fd = sockets[i];
        Check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                         sizeof timeout) == 0 &&
                      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                                 sizeof timeout) == 0,
              "Could not bound HTTP test socket operations");
    }
}

static string ReadBytes(int fd, size_t length) {
    string result;
    result.resize(length);
    size_t offset = 0;
    while (offset < length) {
        ssize_t received = read(fd, &result[offset], length - offset);
        Check(received > 0, "Could not read an HTTP test response");
        offset += received;
    }
    return result;
}

static void CheckRequestHead() {
    int sockets[2];
    CreateSocketPair(sockets, "Could not create HTTP test sockets");
    ConnectionQueue queue;
    http::Connection connection(sockets[1], queue.GetDescriptor(), 5);

    const char request[] =
            "GET /jpip?cid=7 HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Accept-Encoding: gzip\r\n"
            "\r\n";
    Check(write(sockets[0], request, sizeof request - 1) == sizeof request - 1,
          "Could not write an HTTP request head");

    http::RequestHead head;
    Check(connection.ReadRequestHead(&head) == http::Connection::REQUEST_READY,
          "Could not read a complete HTTP request head");
    Check(head.line == "GET /jpip?cid=7 HTTP/1.1",
          "HTTP request line was changed");
    Check(head.accepts_gzip, "HTTP gzip support was not detected");

    close(sockets[0]);
    close(sockets[1]);
}

static void CheckRequestLimit() {
    int sockets[2];
    CreateSocketPair(sockets, "Could not create request-limit sockets");
    ConnectionQueue queue;
    http::Connection connection(sockets[1], queue.GetDescriptor(), 5);
    string request(4097, 'x');
    Check(write(sockets[0], request.data(), request.size()) ==
                  static_cast<ssize_t>(request.size()),
          "Could not write an oversized request head");

    http::RequestHead head;
    Check(connection.ReadRequestHead(&head) ==
                  http::Connection::REQUEST_TOO_LARGE,
          "Oversized HTTP request head was not identified");

    close(sockets[0]);
    close(sockets[1]);
}

static void CheckInterrupt() {
    int sockets[2];
    CreateSocketPair(sockets, "Could not create interrupted-request sockets");
    ConnectionQueue queue;
    http::Connection connection(sockets[1], queue.GetDescriptor(), 5);
    const char partial[] = "GET /jpip?cid=7";
    Check(write(sockets[0], partial, sizeof partial - 1) == sizeof partial - 1,
          "Could not write a partial request");
    Check(queue.Push(42), "Could not queue a replacement connection");

    http::RequestHead head;
    Check(connection.ReadRequestHead(&head) == http::Connection::INTERRUPTED,
          "Replacement did not interrupt a partial request");
    int fd;
    Check(queue.Pop(&fd) && fd == 42,
          "Replacement connection was lost after interrupting a request");

    close(sockets[0]);
    close(sockets[1]);
}

static void CheckResponseFraming() {
    int sockets[2];
    CreateSocketPair(sockets, "Could not create HTTP response sockets");
    ConnectionQueue queue;
    http::Connection connection(sockets[1], queue.GetDescriptor(), 5);

    const string headers = "Content-Length: 0\r\n";
    const string response = "HTTP/1.1 200 OK\r\n" + headers + "\r\n";
    Check(connection.SendOK(headers), "Could not send an HTTP response head");
    Check(ReadBytes(sockets[0], response.size()) == response,
          "HTTP response head changed");

    int send_buffer = 1024;
    Check(setsockopt(sockets[1], SOL_SOCKET, SO_SNDBUF, &send_buffer,
                     sizeof send_buffer) == 0,
          "Could not reduce the HTTP test send buffer");
    string payload(256 * 1024, 'x');
    bool sent = false;
    thread sender([&] { sent = connection.SendChunk(payload.data(), payload.size()); });
    string chunk = ReadBytes(sockets[0], 7 + payload.size() + 2);
    sender.join();
    Check(sent, "Could not send a partial-write HTTP chunk");
    Check(chunk.compare(0, 7, "40000\r\n") == 0,
          "Wrong HTTP chunk length");
    Check(chunk.compare(7, payload.size(), payload) == 0,
          "HTTP chunk payload changed");
    Check(chunk.compare(7 + payload.size(), 2, "\r\n") == 0,
          "HTTP chunk terminator changed");

    close(sockets[0]);
    close(sockets[1]);
}

int main() {
    CheckRequestHead();
    CheckRequestLimit();
    CheckInterrupt();
    CheckResponseFraming();
    return EXIT_SUCCESS;
}
