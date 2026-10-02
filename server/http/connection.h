#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>

#include "request_head.h"

namespace server {

// One HTTP connection: its socket, request-head parser, deadlines, and the
// response in flight. All of this is used only on the connection's strand.
// The commands post to that strand and may be called from any thread.
class Connection : public std::enable_shared_from_this<Connection> {
public:
    enum class ReadFailure {
        MALFORMED,
        TOO_LARGE
    };

    // One step of a generated response.
    struct Chunk {
        enum Status { MORE, COMPLETE, FAILED };

        Status status;
        int length;           // Payload bytes placed in the buffer.
        // FAILED: the complete HTTP response to send instead, used only if
        // the generated response has not started.
        std::string failure;
    };

    // A chunked response whose body is generated on the connection's strand,
    // a chunk at a time, and written while the socket accepts data.
    struct Response {
        std::string headers;  // Sent with the first chunk.
        // Must not throw. Not called again after COMPLETE or FAILED.
        std::function<Chunk(char *buffer, int capacity)> generate;
        // Called exactly once, when `generate` will not be called again and
        // before the peer can observe the outcome of a failed response.
        // True only if the whole response was handed to the socket.
        std::function<void(bool complete)> done;
        bool close;           // Close the connection after the response.
    };

    // Reports, called on the connection's strand. After `request` or
    // `read_failed` the connection reads nothing more until it is answered
    // with Reply(), Respond() or Abort().
    struct Events {
        std::function<void(RequestHead &&)> request;
        // An invalid head of a request that named a JPIP route.
        std::function<void(ReadFailure, const std::string &target)> read_failed;
        // No answer arrived within the connection timeout.
        std::function<void()> blocked;
        // Called once, before the socket closes.
        std::function<void()> closed;
    };

private:
    enum class Deadline {
        NONE,
        IDENTIFICATION,
        READ,
        WRITE,
        BLOCKED
    };

    typedef std::chrono::steady_clock Clock;

    asio::ip::tcp::socket socket;
    asio::strand<asio::io_context::executor_type> strand;
    asio::steady_timer timer;
    RequestHeadParser parser;
    const Events events;
    const int initial_timeout;
    const int connection_timeout;
    const int chunk_size;
    char incoming[4096];
    // The bytes of `incoming` after a complete request head. They are parsed
    // when its response has finished; nothing is read until then.
    std::size_t unparsed_offset = 0;
    std::size_t unparsed_size = 0;
    Deadline deadline = Deadline::NONE;
    Clock::time_point deadline_at;
    Clock::time_point timer_at;
    std::uint64_t timer_epoch = 0;
    bool timer_armed = false;
    bool paused = false;
    bool reading = false;
    bool replying = false;
    bool closing = false;

    std::unique_ptr<Response> response;
    std::vector<char> payload;
    std::string first;
    std::string last;
    bool headers_sent = false;
    bool final = false;

    void DoStart();
    void DoReply(std::string text);
    void DoRespond(Response next);
    void DoAbort();
    void SetDeadline(Deadline reason);
    void ArmTimer();
    void TimerExpired(std::uint64_t epoch);
    void StartReading();
    void Consume(std::size_t offset, std::size_t size);
    void Produce();
    void Substitute(std::string text);
    void EndResponse(bool complete);
    void ResponseCompleted();
    void CloseGracefully();

public:
    Connection(asio::io_context &io, int initial_timeout, int connection_timeout,
               int chunk_size, Events events);

    // For accepting into; not to be used after Start().
    asio::ip::tcp::socket &Socket() {
        return socket;
    }

    void Start();
    // Send a complete HTTP response, then close the connection.
    void Reply(std::string text);
    void Respond(Response next);
    void Abort();

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
};

}
