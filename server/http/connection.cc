#include "connection.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <system_error>
#include <utility>

#include <asio/bind_executor.hpp>
#include <asio/buffer.hpp>
#include <asio/defer.hpp>
#include <asio/error.hpp>
#include <asio/post.hpp>
#include <asio/write.hpp>

using namespace std;

namespace {

// Chunks generated before the strand is yielded to the connection's timer
// and commands.
const int BURST_CHUNKS = 16;

}

namespace server {

using asio::ip::tcp;

Connection::Connection(asio::io_context &io, int _initial_timeout,
                       int _connection_timeout, int _chunk_size, Events _events)
    : socket(io), strand(asio::make_strand(io)), timer(io),
      events(std::move(_events)), initial_timeout(_initial_timeout),
      connection_timeout(_connection_timeout), chunk_size(_chunk_size) {
}

void Connection::Start() {
    auto self = shared_from_this();
    asio::post(strand, [self] { self->DoStart(); });
}

void Connection::Reply(string text) {
    auto self = shared_from_this();
    shared_ptr<string> data = make_shared<string>(std::move(text));
    asio::post(strand, [self, data] { self->DoReply(std::move(*data)); });
}

void Connection::Respond(Response next) {
    auto self = shared_from_this();
    shared_ptr<Response> data = make_shared<Response>(std::move(next));
    asio::post(strand, [self, data] { self->DoRespond(std::move(*data)); });
}

void Connection::Abort() {
    auto self = shared_from_this();
    asio::post(strand, [self] { self->DoAbort(); });
}

void Connection::DoStart() {
    error_code error;
    socket.set_option(tcp::no_delay(true), error);
    if (!error)
        socket.set_option(asio::socket_base::send_buffer_size(524288), error);
    if (!error)
        socket.non_blocking(true, error);
    if (error) {
        DoAbort();
        return;
    }
    SetDeadline(Deadline::IDENTIFICATION);
    StartReading();
}

void Connection::SetDeadline(Deadline reason) {
    deadline = reason;
    if (reason == Deadline::NONE)
        return;
    deadline_at = Clock::now() + chrono::seconds(
            reason == Deadline::IDENTIFICATION ? initial_timeout
                                               : connection_timeout);
    ArmTimer();
}

// One pending wait serves every deadline at or after its expiry, so write
// progress only has to move `deadline_at`.
void Connection::ArmTimer() {
    if (timer_armed && timer_at <= deadline_at)
        return;
    timer_armed = true;
    timer_at = deadline_at;
    uint64_t epoch = ++timer_epoch;
    auto self = shared_from_this();
    timer.expires_at(deadline_at);
    timer.async_wait(asio::bind_executor(strand, [self, epoch](error_code) {
        self->TimerExpired(epoch);
    }));
}

void Connection::TimerExpired(uint64_t epoch) {
    if (epoch != timer_epoch)
        return;
    timer_armed = false;
    if (closing || deadline == Deadline::NONE)
        return;
    if (Clock::now() < deadline_at) {
        ArmTimer();
        return;
    }
    Deadline expired = deadline;
    deadline = Deadline::NONE;
    if (expired == Deadline::BLOCKED)
        events.blocked();
    else
        DoAbort();
}

void Connection::StartReading() {
    if (reading || paused || closing)
        return;
    if (unparsed_size > 0) {
        size_t size = unparsed_size;
        unparsed_size = 0;
        Consume(unparsed_offset, size);
    }
    if (reading || paused || closing)
        return;
    reading = true;
    auto self = shared_from_this();
    socket.async_read_some(asio::buffer(incoming), asio::bind_executor(
            strand, [self](error_code error, size_t size) {
        self->reading = false;
        if (self->closing)
            return;
        if (error) {
            self->DoAbort();
            return;
        }
        self->Consume(0, size);
        self->StartReading();
    }));
}

void Connection::Consume(size_t offset, size_t size) {
    size_t consumed;
    RequestHeadParser::Result result =
            parser.Parse(incoming + offset, size, &consumed);
    if (deadline == Deadline::IDENTIFICATION &&
        parser.HasCompleteJPIPRequestLine())
        SetDeadline(Deadline::READ);
    if (result == RequestHeadParser::INCOMPLETE)
        return;

    // Every other outcome waits for an answer.
    paused = true;
    if (result != RequestHeadParser::COMPLETE) {
        if (!parser.HasJPIPRoute()) {
            DoAbort();
            return;
        }
        SetDeadline(Deadline::BLOCKED);
        events.read_failed(result == RequestHeadParser::TOO_LARGE
                                   ? ReadFailure::TOO_LARGE
                                   : ReadFailure::MALFORMED,
                           parser.GetTarget());
        return;
    }
    unparsed_offset = offset + consumed;
    unparsed_size = size - consumed;
    SetDeadline(Deadline::BLOCKED);
    events.request(parser.TakeRequest());
}

void Connection::DoReply(string text) {
    if (closing || replying || response)
        return;
    replying = true;
    paused = true;
    SetDeadline(Deadline::WRITE);
    first = std::move(text);
    auto self = shared_from_this();
    asio::async_write(socket, asio::buffer(first), asio::bind_executor(
            strand, [self](error_code error, size_t) {
        if (self->closing)
            return;
        if (error)
            self->DoAbort();
        else
            self->CloseGracefully();
    }));
}

void Connection::DoRespond(Response next) {
    if (closing || replying || response) {
        next.done(false);
        return;
    }
    response.reset(new Response(std::move(next)));
    payload.resize(static_cast<size_t>(chunk_size));
    headers_sent = false;
    final = false;
    Produce();
}

// Generate and write on this thread for as long as the socket accepts data.
void Connection::Produce() {
    auto self = shared_from_this();
    for (int i = 0; i < BURST_CHUNKS; ++i) {
        Chunk chunk = response->generate(payload.data(), chunk_size);
        Clock::time_point now = Clock::now();
        if (chunk.status == Chunk::FAILED) {
            if (headers_sent || chunk.failure.empty())
                DoAbort();
            else
                Substitute(std::move(chunk.failure));
            return;
        }
        final = chunk.status == Chunk::COMPLETE;

        first.clear();
        last.clear();
        if (!headers_sent)
            first = response->headers;
        if (chunk.length > 0) {
            char text[2 * sizeof(size_t) + 3];
            first.append(text, static_cast<size_t>(snprintf(
                    text, sizeof text, "%zx\r\n",
                    static_cast<size_t>(chunk.length))));
            last = final ? "\r\n0\r\n\r\n" : "\r\n";
        } else if (final) {
            first += "0\r\n\r\n";
        }
        headers_sent = true;
        deadline = Deadline::WRITE;
        deadline_at = now + chrono::seconds(connection_timeout);
        ArmTimer();

        array<asio::const_buffer, 3> buffers = {{
            asio::buffer(first),
            asio::buffer(payload.data(), static_cast<size_t>(chunk.length)),
            asio::buffer(last)}};
        size_t total = first.size() + static_cast<size_t>(chunk.length) +
                last.size();
        if (total == 0)
            continue;  // Progress without output.
        error_code error;
        size_t sent = socket.write_some(buffers, error);
        if (error == asio::error::would_block || error == asio::error::try_again) {
            sent = 0;
        } else if (error) {
            DoAbort();
            return;
        }
        if (sent < total) {
            // The socket is full: wait for it to take the rest of this chunk.
            for (asio::const_buffer &buffer : buffers) {
                size_t skip = min(sent, buffer.size());
                buffer += skip;
                sent -= skip;
            }
            asio::async_write(socket, buffers, asio::bind_executor(
                    strand, [self](error_code failed, size_t) {
                if (!self->response)
                    return;
                if (failed) {
                    self->DoAbort();
                    return;
                }
                self->deadline_at = Clock::now() +
                        chrono::seconds(self->connection_timeout);
                if (self->final)
                    self->ResponseCompleted();
                else
                    self->Produce();
            }));
            return;
        }
        if (final) {
            ResponseCompleted();
            return;
        }
    }
    asio::defer(strand, [self] {
        if (self->response)
            self->Produce();
    });
}

// Replace a response that has not started with a complete one.
void Connection::Substitute(string text) {
    EndResponse(false);
    DoReply(std::move(text));
}

void Connection::EndResponse(bool complete) {
    if (!response)
        return;
    function<void(bool)> done = std::move(response->done);
    response.reset();
    // A cancelled async_write still owns its buffers until its callback.
    // That callback holds the connection alive.
    if (complete)
        vector<char>().swap(payload);
    done(complete);
}

void Connection::ResponseCompleted() {
    bool close = response->close;
    EndResponse(true);
    if (close) {
        CloseGracefully();
        return;
    }
    paused = false;
    SetDeadline(Deadline::READ);
    StartReading();
}

// The reports precede the socket operations so that their receiver is never
// behind what the peer can observe.
void Connection::CloseGracefully() {
    if (closing)
        return;
    closing = true;
    ++timer_epoch;
    timer.cancel();
    events.closed();
    error_code ignored;
    socket.shutdown(tcp::socket::shutdown_send, ignored);
    socket.close(ignored);
}

void Connection::DoAbort() {
    if (closing)
        return;
    closing = true;
    ++timer_epoch;
    timer.cancel();
    EndResponse(false);
    events.closed();
    error_code ignored;
    socket.close(ignored);
}

}
