#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <uv.h>

#include "jpip/query.h"
#include "jpip/request.h"
#include "server/connection.h"

using namespace std;

namespace {

void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

void CheckRequestSyntax() {
    const char *invalid[] = {
        "GET /movie.jpx?cnew=http HTTP/1.0\r\nHost: localhost\r\n\r\n",
        "GET /movie.jpx?cnew=http HTTP/1.1junk\r\nHost: localhost\r\n\r\n",
        "GET /jpip?cid=7 HTTP/1.1 trailing\r\nHost: localhost\r\n\r\n"
    };
    for (const char *head : invalid) {
        server::RequestHeadParser parser;
        size_t consumed;
        Check(parser.Parse(head, strlen(head), &consumed) ==
                      server::RequestHeadParser::MALFORMED,
              "The HTTP parser accepted an invalid request line");
    }
}

void CheckLongRequestTarget() {
    const string prefix = "/image.jp2?padding=";
    const size_t route_offsets[] = {1000, 1037};
    for (size_t i = 0; i < 2; ++i) {
        string target = prefix +
                string(route_offsets[i] - prefix.size(), 'x') + "&cnew=http";
        string head = "GET " + target + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
        server::RequestHeadParser parser;
        size_t consumed;
        Check(parser.Parse(head.data(), head.size(), &consumed) ==
                      server::RequestHeadParser::COMPLETE,
              "Could not parse the routing-limit request");

        jpip::Request request;
        Check(request.ParseTarget(target),
              "Could not fully parse the routing-limit target");
        Check(parser.HasJPIPRoute() && request.has.cnew,
              "A bounded request target was truncated before JPIP parsing");
    }
}

void CheckRouteClassification() {
    server::RequestHeadParser parser;
    size_t consumed;
    const string routed =
            "GET /image.jp2?cnew=http HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Check(parser.Parse(routed.data(), routed.size(), &consumed) ==
                  server::RequestHeadParser::COMPLETE &&
                  parser.HasJPIPRoute(),
          "Could not classify a complete JPIP request line");
    parser.TakeRequest();

    const string unrelated =
            "GET /status HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Check(parser.Parse(unrelated.data(), unrelated.size(), &consumed) ==
                  server::RequestHeadParser::COMPLETE &&
                  !parser.HasJPIPRoute(),
          "JPIP route classification survived parser reset");

    const char *not_routes[] = {
        "/status?notcnew=http",
        "/status?cnewer=http",
        "/status?value=cnew",
        "/status?x=cid",
        "/status?close=cclose"
    };
    for (const char *target : not_routes)
        Check(!jpip::HasRoutingParameter(target),
              "A non-routing query field was classified as a JPIP route");
    Check(jpip::HasRoutingParameter("/image.jp2?cnew") &&
              jpip::HasRoutingParameter("/image.jp2?x=1&cid=7") &&
              jpip::HasRoutingParameter("/image.jp2?cclose=7&x=1"),
          "A JPIP routing field was not recognized");

    server::RequestHeadParser partial_parser;
    const string oversized = "GET /image.jp2?cnew=http&padding=" +
            string(2050, 'x');
    Check(partial_parser.Parse(oversized.data(), oversized.size(), &consumed) ==
                  server::RequestHeadParser::TOO_LARGE &&
                  partial_parser.HasJPIPRoute(),
          "An oversized partial JPIP request lost route identification");
}

struct Exchange {
    uv_loop_t loop;
    uv_tcp_t listener;
    unique_ptr<server::Connection> connection;
    server::Connection::Write writes[3];
    vector<string> requests;
    string response;
    uint16_t port = 0;
    atomic<bool> failed{false};
    bool closed = false;

    static void Accepted(uv_stream_t *listener, int status) {
        Exchange *self = static_cast<Exchange *>(listener->data);
        if (status != 0) {
            self->failed.store(true);
            uv_close(reinterpret_cast<uv_handle_t *>(&self->listener), NULL);
            return;
        }

        self->connection.reset(new server::Connection(
                1, 3,
                [self](server::Connection &connection,
                       server::RequestHead &&request) {
                    self->requests.push_back(request.target);
                    connection.StartResponse();
                    size_t index = self->requests.size() == 1 ? 0 : 2;
                    if (index == 0) {
                        bool first = connection.Send(
                                &self->writes[0], "one",
                                [self](bool ok) {
                                    if (!ok)
                                        self->failed.store(true);
                                });
                        bool second = connection.Send(
                                &self->writes[1], "two", [self](bool ok) {
                                    if (!ok)
                                        self->failed.store(true);
                                    if (ok)
                                        self->connection->FinishResponse();
                                });
                        if (!first || !second)
                            self->failed.store(true);
                    } else {
                        bool sent = connection.Send(
                                &self->writes[index], "three",
                                [self](bool ok) {
                                    if (!ok)
                                        self->failed.store(true);
                                    if (ok)
                                        self->connection->CloseGracefully();
                                });
                        if (!sent)
                            self->failed.store(true);
                    }
                },
                [self](server::Connection &connection,
                       server::Connection::ReadFailure failure) {
                    self->failed.store(true);
                    connection.Abort();
                },
                [self](server::Connection &connection) {
                    self->failed.store(true);
                    connection.Abort();
                },
                [self](server::Connection &connection) {
                    self->closed = true;
                    uv_close(reinterpret_cast<uv_handle_t *>(&self->listener),
                             NULL);
                }));
        if (!self->connection->Initialize(&self->loop) ||
            !self->connection->Accept(listener) ||
            !self->connection->Start()) {
            self->failed.store(true);
            self->connection->Abort();
        }
    }

    void Run() {
        Check(uv_loop_init(&loop) == 0, "Could not initialize the test loop");
        Check(uv_tcp_init(&loop, &listener) == 0,
              "Could not initialize the test listener");
        listener.data = this;

        sockaddr_in address;
        Check(uv_ip4_addr("127.0.0.1", 0, &address) == 0 &&
                      uv_tcp_bind(&listener,
                                  reinterpret_cast<const sockaddr *>(&address),
                                  0) == 0 &&
                      uv_listen(reinterpret_cast<uv_stream_t *>(&listener), 1,
                                Accepted) == 0,
              "Could not start the test listener");
        int length = sizeof address;
        Check(uv_tcp_getsockname(&listener,
                                 reinterpret_cast<sockaddr *>(&address),
                                 &length) == 0,
              "Could not read the test listener address");
        port = ntohs(address.sin_port);

        thread client([this] {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                failed.store(true);
                return;
            }
            timeval timeout = {5, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            sockaddr_in address;
            uv_ip4_addr("127.0.0.1", port, &address);
            if (connect(fd, reinterpret_cast<sockaddr *>(&address),
                        sizeof address) != 0) {
                failed.store(true);
                close(fd);
                return;
            }
            const string request_line =
                    "GET /first?cnew=http HTTP/1.1\r\n";
            if (write(fd, request_line.data(), request_line.size()) !=
                static_cast<ssize_t>(request_line.size()))
                failed.store(true);
            this_thread::sleep_for(chrono::milliseconds(1500));
            const string remainder =
                    "Host: localhost\r\n\r\n"
                    "GET /second?cnew=http HTTP/1.1\r\n"
                    "Host: localhost\r\n\r\n";
            if (write(fd, remainder.data(), remainder.size()) !=
                static_cast<ssize_t>(remainder.size()))
                failed.store(true);
            char buffer[64];
            ssize_t received;
            while ((received = read(fd, buffer, sizeof buffer)) > 0)
                response.append(buffer, received);
            if (received < 0)
                failed.store(true);
            close(fd);
        });

        uv_run(&loop, UV_RUN_DEFAULT);
        client.join();
        connection.reset();
        Check(uv_loop_close(&loop) == 0, "The test loop retained handles");
    }
};

struct FailureExchange {
    enum class Mode {
        TOO_LARGE,
        IDENTIFICATION_TIMEOUT,
        REQUEST_HEAD_TIMEOUT
    };

    uv_loop_t loop;
    uv_tcp_t listener;
    unique_ptr<server::Connection> connection;
    server::Connection::Write response_write;
    Mode mode;
    string response;
    uint16_t port = 0;
    atomic<bool> failed{false};
    bool closed = false;
    bool observed = false;
    chrono::milliseconds elapsed{0};

    explicit FailureExchange(Mode _mode) : mode(_mode) {
    }

    static void Accepted(uv_stream_t *listener, int status) {
        FailureExchange *self =
                static_cast<FailureExchange *>(listener->data);
        if (status != 0) {
            self->failed.store(true);
            uv_close(reinterpret_cast<uv_handle_t *>(&self->listener), NULL);
            return;
        }

        self->connection.reset(new server::Connection(
                1, 2,
                [self](server::Connection &connection,
                       server::RequestHead &&request) {
                    self->failed.store(true);
                    connection.Abort();
                },
                [self](server::Connection &connection,
                       server::Connection::ReadFailure failure) {
                    if (self->mode != Mode::TOO_LARGE ||
                        failure != server::Connection::ReadFailure::TOO_LARGE) {
                        self->failed.store(true);
                        connection.Abort();
                        return;
                    }
                    self->observed = true;
                    connection.StartResponse();
                    bool sent = connection.Send(
                            &self->response_write, "HTTP/1.1 431 Too Large\r\n"
                                          "Connection: close\r\n\r\n",
                            [self](bool ok) {
                                if (!ok)
                                    self->failed.store(true);
                                self->connection->CloseGracefully();
                            });
                    if (!sent) {
                        self->failed.store(true);
                        connection.Abort();
                    }
                },
                [self](server::Connection &connection) {
                    if (self->mode == Mode::TOO_LARGE ||
                        (self->mode == Mode::REQUEST_HEAD_TIMEOUT &&
                         (!connection.HasJPIPRoute() ||
                          connection.GetRequestTarget() !=
                                  "/partial?cnew=http")))
                        self->failed.store(true);
                    self->observed = true;
                    connection.CloseGracefully();
                },
                [self](server::Connection &connection) {
                    self->closed = true;
                    uv_close(reinterpret_cast<uv_handle_t *>(&self->listener),
                             NULL);
                }));
        if (!self->connection->Initialize(&self->loop) ||
            !self->connection->Accept(listener) ||
            !self->connection->Start()) {
            self->failed.store(true);
            self->connection->Abort();
        }
    }

    void Run() {
        Check(uv_loop_init(&loop) == 0, "Could not initialize the test loop");
        Check(uv_tcp_init(&loop, &listener) == 0,
              "Could not initialize the test listener");
        listener.data = this;

        sockaddr_in address;
        Check(uv_ip4_addr("127.0.0.1", 0, &address) == 0 &&
                      uv_tcp_bind(&listener,
                                  reinterpret_cast<const sockaddr *>(&address),
                                  0) == 0 &&
                      uv_listen(reinterpret_cast<uv_stream_t *>(&listener), 1,
                                Accepted) == 0,
              "Could not start the test listener");
        int length = sizeof address;
        Check(uv_tcp_getsockname(&listener,
                                 reinterpret_cast<sockaddr *>(&address),
                                 &length) == 0,
              "Could not read the test listener address");
        port = ntohs(address.sin_port);

        thread client([this] {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                failed.store(true);
                return;
            }
            timeval timeout = {5, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            sockaddr_in address;
            uv_ip4_addr("127.0.0.1", port, &address);
            if (connect(fd, reinterpret_cast<sockaddr *>(&address),
                        sizeof address) != 0) {
                failed.store(true);
                close(fd);
                return;
            }
            string request;
            if (mode == Mode::TOO_LARGE)
                request = "GET /" + string(2050, 'a');
            else if (mode == Mode::IDENTIFICATION_TIMEOUT)
                request = "GET /partial";
            else
                request = "GET /partial?cnew=http HTTP/1.1\r\nHost: l";
            chrono::steady_clock::time_point start = chrono::steady_clock::now();
            if (write(fd, request.data(), request.size()) !=
                static_cast<ssize_t>(request.size()))
                failed.store(true);
            if (mode == Mode::REQUEST_HEAD_TIMEOUT) {
                this_thread::sleep_for(chrono::milliseconds(750));
                if (write(fd, "o", 1) != 1)
                    failed.store(true);
                this_thread::sleep_for(chrono::milliseconds(750));
                if (write(fd, "c", 1) != 1)
                    failed.store(true);
            }
            char buffer[256];
            ssize_t received;
            while ((received = read(fd, buffer, sizeof buffer)) > 0)
                response.append(buffer, received);
            if (received < 0)
                failed.store(true);
            elapsed = chrono::duration_cast<chrono::milliseconds>(
                    chrono::steady_clock::now() - start);
            close(fd);
        });

        uv_run(&loop, UV_RUN_DEFAULT);
        client.join();
        connection.reset();
        Check(uv_loop_close(&loop) == 0, "The test loop retained handles");
    }
};

}

int main() {
    signal(SIGPIPE, SIG_IGN);
    CheckRequestSyntax();
    CheckLongRequestTarget();
    CheckRouteClassification();

    Exchange exchange;
    exchange.Run();
    Check(!exchange.failed.load(), "The libuv connection exchange failed");
    Check(exchange.closed, "The libuv connection did not close");
    Check(exchange.requests.size() == 2 &&
                  exchange.requests[0] == "/first?cnew=http" &&
                  exchange.requests[1] == "/second?cnew=http",
          "Pipelined requests were not delivered in order");
    Check(exchange.response == "onetwothree",
          "Writes were not delivered before graceful close");

    FailureExchange too_large(FailureExchange::Mode::TOO_LARGE);
    too_large.Run();
    Check(!too_large.failed.load() && too_large.observed && too_large.closed,
          "The oversized request head was not handled");
    Check(too_large.response.find("HTTP/1.1 431") == 0,
          "The oversized request response was not delivered");

    FailureExchange timeout(FailureExchange::Mode::IDENTIFICATION_TIMEOUT);
    timeout.Run();
    Check(!timeout.failed.load() && timeout.observed && timeout.closed,
          "The identification deadline was not enforced");
    Check(timeout.response.empty(),
          "The identification timeout unexpectedly wrote a response");

    FailureExchange trickle(FailureExchange::Mode::REQUEST_HEAD_TIMEOUT);
    trickle.Run();
    Check(!trickle.failed.load() && trickle.observed && trickle.closed &&
                  trickle.response.empty() &&
                  trickle.elapsed >= chrono::milliseconds(1700) &&
                  trickle.elapsed < chrono::milliseconds(3000),
          "Incoming header bytes extended the request-head deadline");
    return EXIT_SUCCESS;
}
