#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <uv.h>

#include "server/http/connection.h"

using namespace std;

namespace {

void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

struct Exchange {
    uv_loop_t loop;
    uv_tcp_t listener;
    uv_timer_t response_timer;
    unique_ptr<server::Connection> connection;
    server::Connection::Write writes[2];
    vector<string> requests;
    string response;
    uint16_t port = 0;
    atomic<bool> failed{false};
    bool closed = false;
    bool response_paused_reads = false;

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
                    if (self->requests.size() == 1) {
                        bool first = connection.Send(
                                &self->writes[0], "one",
                                [self](bool ok) {
                                    if (!ok)
                                        self->failed.store(true);
                                });
                        if (!self->writes[0].active ||
                            connection.Send(&self->writes[0], "unexpected",
                                            [self](bool) {
                                                self->failed.store(true);
                                            }))
                            self->failed.store(true);
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
                        // Leave no write pending while checking the read state.
                        if (uv_timer_start(&self->response_timer, RespondSecond,
                                           0, 0) != 0)
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
                    uv_close(reinterpret_cast<uv_handle_t *>(&self->response_timer),
                             NULL);
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

    static void RespondSecond(uv_timer_t *timer) {
        Exchange *self = static_cast<Exchange *>(timer->data);
        // Both writes of the first response have completed. The second
        // response is active, but has not submitted a write yet, so an active
        // connection socket here means reads have been restarted.
        uv_walk(&self->loop, [](uv_handle_t *handle, void *data) {
            Exchange *self = static_cast<Exchange *>(data);
            if (handle->type == UV_TCP &&
                handle->data == self->connection.get())
                self->response_paused_reads = uv_is_active(handle) == 0;
        }, self);
        bool sent = self->connection->Send(
                &self->writes[0], "three", [self](bool ok) {
                    if (!ok)
                        self->failed.store(true);
                    if (ok)
                        self->connection->CloseGracefully();
                });
        if (!sent) {
            self->failed.store(true);
            self->connection->Abort();
        }
    }

    void Run() {
        Check(uv_loop_init(&loop) == 0, "Could not initialize the test loop");
        Check(uv_timer_init(&loop, &response_timer) == 0,
              "Could not initialize the response timer");
        response_timer.data = this;
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

void CheckRejectedWrites() {
    uv_loop_t loop;
    Check(uv_loop_init(&loop) == 0, "Could not initialize rejected-write loop");
    int completions = 0;
    int closures = 0;
    server::Connection connection(
            0, 0,
            [](server::Connection &, server::RequestHead &&) {
                Check(false, "Unconnected socket delivered a request");
            },
            [](server::Connection &, server::Connection::ReadFailure) {
                Check(false, "Unconnected socket reported a read");
            },
            [](server::Connection &) {
                Check(false, "Disabled deadline expired");
            },
            [&closures](server::Connection &) { ++closures; });
    Check(connection.Initialize(&loop), "Could not initialize unconnected socket");
    server::Connection::Write write;
    function<void(bool)> completed = [&completions](bool) { ++completions; };
    Check(!connection.Send(&write, "", completed) && !write.active,
          "Empty write became active");
    Check(!connection.Send(&write, "unconnected", completed) && !write.active,
          "Failed write submission became active");
    connection.CloseGracefully();
    Check(!connection.Send(&write, "closing", completed) && !write.active,
          "Closing connection accepted a write");
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(completions == 0 && closures == 1 && uv_loop_close(&loop) == 0,
          "Rejected writes retained pending work or changed callback lifetime");
}

}

int main() {
    signal(SIGPIPE, SIG_IGN);
    CheckRejectedWrites();

    Exchange exchange;
    exchange.Run();
    Check(!exchange.failed.load(), "The libuv connection exchange failed");
    Check(exchange.closed, "The libuv connection did not close");
    Check(exchange.response_paused_reads,
          "Reads resumed during the retained request's response");
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
