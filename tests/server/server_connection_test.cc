// Direct tests of server::Connection: report order, deadlines, response
// framing, backpressure and closure, with scripted responses and a blocking
// client. No JPIP or JPEG 2000 code is involved.
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "server/http/connection.h"

using namespace std;
using server::Connection;

namespace {

typedef chrono::steady_clock Clock;

void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

long ElapsedMs(Clock::time_point start) {
    return static_cast<long>(chrono::duration_cast<chrono::milliseconds>(
            Clock::now() - start).count());
}

// One accepted connection served by two I/O threads, and its reports.
struct Fixture {
    asio::io_context io;
    asio::executor_work_guard<asio::io_context::executor_type> work;
    asio::ip::tcp::acceptor acceptor;
    shared_ptr<Connection> connection;
    vector<thread> threads;
    mutex guard;
    vector<string> events;
    function<void(const string &target)> on_request;
    function<void()> on_read_failed;
    function<void()> on_blocked;
    uint16_t port = 0;

    Fixture(int initial_timeout, int connection_timeout, int chunk_size = 16)
        : work(asio::make_work_guard(io)),
          acceptor(io, asio::ip::tcp::endpoint(
                  asio::ip::address_v4::loopback(), 0)) {
        port = acceptor.local_endpoint().port();
        Connection::Events reports;
        reports.request = [this](server::RequestHead &&head) {
            Record("request " + head.target);
            if (on_request)
                on_request(head.target);
        };
        reports.read_failed = [this](Connection::ReadFailure failure,
                                     const string &target) {
            Record(string(failure == Connection::ReadFailure::TOO_LARGE
                                  ? "too-large " : "malformed ") + target);
            if (on_read_failed)
                on_read_failed();
        };
        reports.blocked = [this] {
            Record("blocked");
            if (on_blocked)
                on_blocked();
        };
        reports.closed = [this] { Record("closed"); };
        connection = make_shared<Connection>(io, initial_timeout,
                                             connection_timeout, chunk_size,
                                             std::move(reports));
        acceptor.async_accept(connection->Socket(), [this](error_code error) {
            Check(!error, "Could not accept the test client");
            acceptor.close();
            connection->Start();
        });
        for (int i = 0; i < 2; ++i)
            threads.emplace_back([this] { io.run(); });
    }

    // Returns only when the connection has left nothing pending.
    ~Fixture() {
        connection.reset();
        work.reset();
        for (thread &running : threads)
            running.join();
    }

    void Record(const string &event) {
        lock_guard<mutex> lock(guard);
        events.push_back(event);
    }

    size_t Count(const string &event) {
        lock_guard<mutex> lock(guard);
        size_t count = 0;
        for (const string &recorded : events)
            count += recorded == event;
        return count;
    }

    void WaitFor(const string &event, size_t count = 1) {
        Clock::time_point deadline = Clock::now() + chrono::seconds(5);
        while (Count(event) < count) {
            Check(Clock::now() < deadline,
                  "An expected connection report did not arrive");
            this_thread::sleep_for(chrono::milliseconds(1));
        }
    }

    bool Reported(const vector<string> &expected) {
        lock_guard<mutex> lock(guard);
        if (events != expected) {
            for (const string &recorded : events)
                cerr << "  reported: " << recorded << endl;
        }
        return events == expected;
    }
};

struct Step {
    Connection::Chunk::Status status;
    string data;
    string failure;
};

// A response that plays `steps` and records its completion.
Connection::Response Scripted(Fixture &fixture, const string &headers,
                              const vector<Step> &steps, bool close) {
    shared_ptr<size_t> next = make_shared<size_t>(0);
    Connection::Response response;
    response.headers = headers;
    response.close = close;
    response.generate = [steps, next](char *buffer, int capacity) {
        Check(*next < steps.size(), "A finished response was generated again");
        const Step &step = steps[(*next)++];
        Check(step.data.size() <= static_cast<size_t>(capacity),
              "A scripted chunk exceeds the connection buffer");
        memcpy(buffer, step.data.data(), step.data.size());
        Connection::Chunk chunk = {step.status,
                                   static_cast<int>(step.data.size()),
                                   step.failure};
        return chunk;
    };
    response.done = [&fixture](bool complete) {
        fixture.Record(complete ? "done complete" : "done failed");
    };
    return response;
}

// A response of full chunks that never completes.
Connection::Response Endless(Fixture &fixture, atomic<int> *generated) {
    Connection::Response response;
    response.headers = "H\r\n\r\n";
    response.close = false;
    response.generate = [generated](char *buffer, int capacity) {
        memset(buffer, 'x', static_cast<size_t>(capacity));
        ++*generated;
        Connection::Chunk chunk = {Connection::Chunk::MORE, capacity, string()};
        return chunk;
    };
    response.done = [&fixture](bool complete) {
        fixture.Record(complete ? "done complete" : "done failed");
    };
    return response;
}

int Connect(uint16_t port, int receive_buffer = 0) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Check(fd >= 0, "Could not create the test client socket");
    timeval timeout = {5, 0};
    Check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0,
          "Could not set the test client timeout");
    if (receive_buffer > 0)
        Check(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer,
                         sizeof receive_buffer) == 0,
              "Could not constrain the test client receive buffer");
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Check(connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0,
          "Could not connect the test client");
    return fd;
}

void Send(int fd, const string &data) {
    Check(write(fd, data.data(), data.size()) ==
                  static_cast<ssize_t>(data.size()),
          "Could not write test client data");
}

// Everything the server sends until it closes the connection.
string ReadAll(int fd) {
    string received;
    char buffer[65536];
    ssize_t length;
    while ((length = read(fd, buffer, sizeof buffer)) > 0)
        received.append(buffer, static_cast<size_t>(length));
    Check(length == 0, "The connection did not close cleanly");
    close(fd);
    return received;
}

// Close with a reset instead of an orderly shutdown.
void Reset(int fd) {
    linger immediate = {1, 0};
    Check(setsockopt(fd, SOL_SOCKET, SO_LINGER, &immediate, sizeof immediate) == 0,
          "Could not arm the test client reset");
    close(fd);
}

// Returns once generation has started and then stopped advancing.
void WaitUntilStalled(const atomic<int> &generated) {
    Clock::time_point deadline = Clock::now() + chrono::seconds(3);
    for (int previous = 0;;) {
        this_thread::sleep_for(chrono::milliseconds(50));
        int current = generated.load();
        if (current > 0 && current == previous)
            return;
        Check(Clock::now() < deadline, "Generation did not stop at backpressure");
        previous = current;
    }
}

const char REQUEST[] = "GET /image?cnew=http HTTP/1.1\r\nHost: localhost\r\n\r\n";

void CheckPipelinedResponses() {
    Fixture fixture(1, 3);
    fixture.on_request = [&fixture](const string &target) {
        if (target == "/first?cnew=http")
            fixture.connection->Respond(Scripted(fixture, "H1\r\n\r\n", {
                    {Connection::Chunk::MORE, "one", ""},
                    {Connection::Chunk::MORE, "", ""},
                    {Connection::Chunk::COMPLETE, "two", ""}}, false));
        else
            fixture.connection->Respond(Scripted(fixture, "H2\r\n\r\n", {
                    {Connection::Chunk::COMPLETE, "three", ""}}, true));
    };
    int fd = Connect(fixture.port);
    Send(fd, "GET /first?cnew=http HTTP/1.1\r\n");
    // A complete JPIP request line ends the identification deadline.
    this_thread::sleep_for(chrono::milliseconds(1500));
    Send(fd, "Host: localhost\r\n\r\n"
             "GET /second?cnew=http HTTP/1.1\r\nHost: localhost\r\n\r\n");
    Check(ReadAll(fd) == "H1\r\n\r\n3\r\none\r\n3\r\ntwo\r\n0\r\n\r\n"
                         "H2\r\n\r\n5\r\nthree\r\n0\r\n\r\n",
          "Responses were not framed and delivered in request order");
    Check(fixture.Reported({"request /first?cnew=http", "done complete",
                            "request /second?cnew=http", "done complete",
                            "closed"}),
          "A retained request was delivered before its predecessor finished");
}

void CheckEmptyFinalChunks() {
    for (bool empty_response : {false, true}) {
        Fixture fixture(1, 3);
        fixture.on_request = [&fixture, empty_response](const string &) {
            vector<Step> steps;
            if (!empty_response)
                steps.push_back({Connection::Chunk::MORE, "abc", ""});
            // Enough empty steps to cross the generation burst boundary.
            for (int i = 0; i < 40; ++i)
                steps.push_back({Connection::Chunk::MORE, "", ""});
            steps.push_back({Connection::Chunk::COMPLETE, "", ""});
            fixture.connection->Respond(Scripted(fixture, "H\r\n\r\n", steps, true));
        };
        int fd = Connect(fixture.port);
        Send(fd, REQUEST);
        Check(ReadAll(fd) == (empty_response ? "H\r\n\r\n0\r\n\r\n"
                                            : "H\r\n\r\n3\r\nabc\r\n0\r\n\r\n"),
              "An empty final chunk lost or duplicated the HTTP terminator");
        Check(fixture.Reported({"request /image?cnew=http", "done complete", "closed"}),
              "An empty final chunk did not complete exactly once");
    }
}

void CheckBurstYields() {
    Fixture fixture(1, 3);
    atomic<int> generated(0);
    fixture.on_request = [&fixture, &generated](const string &) {
        Connection::Response response = Endless(fixture, &generated);
        response.generate = [&fixture, &generated](char *, int) {
            if (++generated == 1)
                fixture.connection->Abort();
            return Connection::Chunk{Connection::Chunk::MORE, 0, string()};
        };
        fixture.connection->Respond(std::move(response));
    };
    int fd = Connect(fixture.port);
    Send(fd, REQUEST);
    Check(ReadAll(fd) == "H\r\n\r\n",
          "Aborting an empty generation burst damaged its headers");
    Check(generated.load() > 0 && generated.load() <= 32,
          "Generation did not yield to the queued abort");
    Check(fixture.Reported({"request /image?cnew=http", "done failed", "closed"}),
          "Aborting a generation burst did not report failure exactly once");
}

void CheckBackpressureRecovery() {
    Fixture fixture(1, 5, 65536);
    const int chunks = 96;
    atomic<int> generated(0);
    fixture.on_request = [&fixture, &generated](const string &) {
        Connection::Response response = Endless(fixture, &generated);
        response.close = true;
        response.generate = [&generated](char *buffer, int capacity) {
            int n = ++generated;
            Check(n <= chunks, "A completed response was generated again");
            memset(buffer, 'a' + n % 26, static_cast<size_t>(capacity));
            return Connection::Chunk{n == chunks ? Connection::Chunk::COMPLETE
                                                 : Connection::Chunk::MORE,
                                     capacity, string()};
        };
        fixture.connection->Respond(std::move(response));
    };
    int fd = Connect(fixture.port, 4096);
    Send(fd, REQUEST);
    fixture.WaitFor("request /image?cnew=http");
    Clock::time_point deadline = Clock::now() + chrono::seconds(3);
    int previous = 0;
    for (;;) {
        this_thread::sleep_for(chrono::milliseconds(50));
        int current = generated.load();
        Check(current < chunks, "The response did not exercise backpressure");
        if (current > 0 && current == previous)
            break;
        Check(Clock::now() < deadline, "Generation did not stop at backpressure");
        previous = current;
    }
    Check(fixture.Count("done complete") == 0 && fixture.Count("done failed") == 0,
          "An unread response completed before the socket accepted it");
    int receive_buffer = 1024 * 1024;
    Check(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer,
                     sizeof receive_buffer) == 0,
          "Could not enlarge the receive buffer for draining");
    string expected = "H\r\n\r\n";
    for (int n = 1; n <= chunks; ++n)
        expected += "10000\r\n" + string(65536, 'a' + n % 26) + "\r\n";
    expected += "0\r\n\r\n";
    Check(ReadAll(fd) == expected,
          "Resuming a backpressured response lost, repeated or changed bytes");
    Check(generated.load() == chunks &&
                  fixture.Reported({"request /image?cnew=http", "done complete", "closed"}),
          "A resumed response did not generate and complete exactly once");
}

void CheckRequestHeadLimits() {
    {
        Fixture fixture(1, 2);
        fixture.on_read_failed = [&fixture] {
            fixture.connection->Reply(
                    "HTTP/1.1 431 Too Large\r\nConnection: close\r\n\r\n");
        };
        int fd = Connect(fixture.port);
        Send(fd, "GET /large?cnew=http HTTP/1.1\r\nHost: l\r\nX: " +
                         string(4096, 'x'));
        Check(ReadAll(fd) == "HTTP/1.1 431 Too Large\r\nConnection: close\r\n\r\n",
              "The oversized request response was not delivered");
        Check(fixture.Reported({"too-large /large?cnew=http", "closed"}),
              "The oversized request head was not reported once");
    }
    {
        Fixture fixture(1, 2);
        int fd = Connect(fixture.port);
        Send(fd, "GET /" + string(2050, 'a'));
        Check(ReadAll(fd).empty() && fixture.Reported({"closed"}),
              "An oversized request without a JPIP route was answered");
    }
}

void CheckReadDeadlines() {
    {
        Fixture fixture(1, 2);
        int fd = Connect(fixture.port);
        Clock::time_point start = Clock::now();
        Send(fd, "GET /partial");
        Check(ReadAll(fd).empty() && fixture.Reported({"closed"}),
              "The identification timeout wrote a response or a report");
        long elapsed = ElapsedMs(start);
        Check(elapsed >= 800 && elapsed < 1900,
              "The identification deadline was not enforced");
    }
    {
        Fixture fixture(1, 2);
        int fd = Connect(fixture.port);
        Clock::time_point start = Clock::now();
        Send(fd, "GET /partial?cnew=http HTTP/1.1\r\nHost: l");
        this_thread::sleep_for(chrono::milliseconds(750));
        Send(fd, "o");
        this_thread::sleep_for(chrono::milliseconds(750));
        Send(fd, "c");
        Check(ReadAll(fd).empty() && fixture.Reported({"closed"}),
              "The request-head timeout wrote a response or a report");
        long elapsed = ElapsedMs(start);
        Check(elapsed >= 1700 && elapsed < 3000,
              "Incoming header bytes extended the request-head deadline");
    }
}

void CheckBlockedRequest() {
    Fixture fixture(1, 1);
    fixture.on_blocked = [&fixture] {
        fixture.connection->Reply("HTTP/1.1 503 Busy\r\n\r\n");
    };
    int fd = Connect(fixture.port);
    Clock::time_point start = Clock::now();
    Send(fd, REQUEST);
    Check(ReadAll(fd) == "HTTP/1.1 503 Busy\r\n\r\n",
          "The reply to a blocked request was not delivered");
    Check(ElapsedMs(start) >= 800 &&
                  fixture.Reported({"request /image?cnew=http", "blocked",
                                    "closed"}),
          "An unanswered request was not reported as blocked once");
}

void CheckResponseFailures() {
    {
        Fixture fixture(1, 2);
        fixture.on_request = [&fixture](const string &) {
            fixture.connection->Respond(Scripted(fixture, "H\r\n\r\n", {
                    {Connection::Chunk::FAILED, "", "HTTP/1.1 500 Failed\r\n\r\n"}}, false));
        };
        int fd = Connect(fixture.port);
        Send(fd, REQUEST);
        Check(ReadAll(fd) == "HTTP/1.1 500 Failed\r\n\r\n",
              "A failure before the headers did not send its own response");
        Check(fixture.Reported({"request /image?cnew=http", "done failed",
                                "closed"}),
              "An early failure was not reported before its response");
    }
    {
        Fixture fixture(1, 2);
        fixture.on_request = [&fixture](const string &) {
            fixture.connection->Respond(Scripted(fixture, "H\r\n\r\n", {
                    {Connection::Chunk::MORE, "abc", ""},
                    {Connection::Chunk::FAILED, "", "HTTP/1.1 500 Failed\r\n\r\n"}}, false));
        };
        int fd = Connect(fixture.port);
        Send(fd, REQUEST);
        Check(ReadAll(fd) == "H\r\n\r\n3\r\nabc\r\n",
              "A failure after the headers rewrote or terminated the response");
        Check(fixture.Reported({"request /image?cnew=http", "done failed",
                                "closed"}),
              "A late failure was not reported once");
    }
}

void CheckBackpressure() {
    Fixture fixture(1, 1, 65536);
    atomic<int> generated(0);
    fixture.on_request = [&fixture, &generated](const string &) {
        fixture.connection->Respond(Endless(fixture, &generated));
    };
    int fd = Connect(fixture.port, 4096);
    Clock::time_point start = Clock::now();
    Send(fd, REQUEST);
    // The client reads nothing: generation must stop once the socket is
    // full, and the write deadline must then end the connection.
    fixture.WaitFor("closed");
    long elapsed = ElapsedMs(start);
    int count = generated.load();
    Check(elapsed >= 800 && elapsed < 3000,
          "A stalled response was not ended by the write deadline");
    Check(count >= 2 && count <= 256,
          "Generation was not bounded by the unread socket");
    Check(fixture.Reported({"request /image?cnew=http", "done failed", "closed"}),
          "A stalled response was not reported once");
    close(fd);
}

// A final chunk larger than the socket takes completes from the write
// callback, not from the generation loop.
void CheckLargeFinalChunk() {
    const size_t size = 4 * 1024 * 1024;
    Fixture fixture(1, 5, static_cast<int>(size));
    fixture.on_request = [&fixture](const string &) {
        fixture.connection->Respond(Scripted(fixture, "H\r\n\r\n", {
                {Connection::Chunk::COMPLETE, string(size, 'z'), ""}}, true));
    };
    int fd = Connect(fixture.port, 4096);
    Send(fd, REQUEST);
    this_thread::sleep_for(chrono::milliseconds(200));
    Check(fixture.Count("done complete") == 0,
          "A response completed before the socket accepted it");
    Check(ReadAll(fd) == "H\r\n\r\n400000\r\n" + string(size, 'z') + "\r\n0\r\n\r\n",
          "A final chunk finished by the write callback lost or changed bytes");
    Check(fixture.Reported({"request /image?cnew=http", "done complete", "closed"}),
          "A final chunk finished by the write callback did not complete once");
}

// The peer resets the connection while a write is pending, and between
// writes of a response it is draining.
void CheckPeerReset() {
    for (bool draining : {false, true}) {
        Fixture fixture(1, 5, 65536);
        atomic<int> generated(0);
        fixture.on_request = [&fixture, &generated](const string &) {
            fixture.connection->Respond(Endless(fixture, &generated));
        };
        int fd = Connect(fixture.port, draining ? 0 : 4096);
        Send(fd, REQUEST);
        fixture.WaitFor("request /image?cnew=http");
        if (draining) {
            char buffer[65536];
            for (size_t total = 0; total < 2 * 1024 * 1024;) {
                ssize_t length = read(fd, buffer, sizeof buffer);
                Check(length > 0, "The response ended before the reset");
                total += static_cast<size_t>(length);
            }
        } else {
            WaitUntilStalled(generated);
        }
        Reset(fd);
        fixture.WaitFor("closed");
        Check(fixture.Reported({"request /image?cnew=http", "done failed", "closed"}),
              "A response reset by its peer was not reported once");
    }
}

// A reply too large for the socket is still pending when the peer resets
// the connection, or when the connection is aborted.
void CheckInterruptedReply() {
    for (bool abort : {false, true}) {
        Fixture fixture(1, 5);
        fixture.on_request = [&fixture](const string &) {
            fixture.connection->Reply(string(4 * 1024 * 1024, 'r'));
        };
        int fd = Connect(fixture.port, 4096);
        Send(fd, REQUEST);
        fixture.WaitFor("request /image?cnew=http");
        this_thread::sleep_for(chrono::milliseconds(100));
        Check(fixture.Count("closed") == 0,
              "A reply larger than the socket completed unread");
        if (abort)
            fixture.connection->Abort();
        else
            Reset(fd);
        fixture.WaitFor("closed");
        Check(fixture.Reported({"request /image?cnew=http", "closed"}),
              "An interrupted reply did not close its connection once");
        if (abort)
            close(fd);
    }
}

void CheckAbort() {
    Fixture fixture(1, 5, 65536);
    atomic<int> generated(0);
    fixture.on_request = [&fixture, &generated](const string &) {
        fixture.connection->Respond(Endless(fixture, &generated));
    };
    int fd = Connect(fixture.port, 4096);
    Send(fd, REQUEST);
    fixture.WaitFor("request /image?cnew=http");
    Clock::time_point deadline = Clock::now() + chrono::seconds(5);
    while (generated.load() < 2) {
        Check(Clock::now() < deadline, "The response did not start generating");
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    fixture.connection->Abort();
    fixture.connection->Abort();
    fixture.WaitFor("closed");
    Check(fixture.Reported({"request /image?cnew=http", "done failed", "closed"}),
          "An aborted response was not reported once, before the closure");
    int count = generated.load();

    // A closed connection refuses a response without generating it, and
    // ignores a reply.
    fixture.connection->Respond(Endless(fixture, &generated));
    fixture.connection->Reply("ignored");
    fixture.WaitFor("done failed", 2);
    Check(generated.load() == count && fixture.Count("closed") == 1,
          "A closed connection generated a response or closed twice");
    close(fd);
}

}

int main() {
    signal(SIGPIPE, SIG_IGN);
    CheckPipelinedResponses();
    CheckEmptyFinalChunks();
    CheckBurstYields();
    CheckBackpressureRecovery();
    CheckRequestHeadLimits();
    CheckReadDeadlines();
    CheckBlockedRequest();
    CheckResponseFailures();
    CheckBackpressure();
    CheckLargeFinalChunk();
    CheckPeerReset();
    CheckInterruptedReply();
    CheckAbort();
    return EXIT_SUCCESS;
}
