#include "server/connection.h"

#include <algorithm>
#include <utility>

using namespace std;

namespace server {

Connection::Connection(int _initial_timeout, int _connection_timeout,
                       RequestReady _request_ready,
                       ReadFailed _read_failed,
                       DeadlineReached _deadline_reached, Closed _closed)
    : request_ready(std::move(_request_ready)),
      read_failed(std::move(_read_failed)),
      deadline_reached(std::move(_deadline_reached)),
      closed(std::move(_closed)), initial_timeout(_initial_timeout),
      connection_timeout(_connection_timeout) {
}

bool Connection::Initialize(uv_loop_t *loop) {
    if (uv_tcp_init(loop, &socket) != 0) {
        NotifyClosed();
        return false;
    }
    socket.data = this;
    socket_initialized = true;
    open_handles++;
    if (uv_timer_init(loop, &timer) != 0) {
        uv_close(reinterpret_cast<uv_handle_t *>(&socket), HandleClosed);
        return false;
    }
    timer.data = this;
    timer_initialized = true;
    open_handles++;
    return true;
}

bool Connection::Accept(uv_stream_t *listener) {
    if (uv_accept(listener, GetStream()) != 0 ||
        uv_tcp_nodelay(&socket, 1) != 0)
        return false;
    int send_buffer_size = 524288;
    return uv_send_buffer_size(reinterpret_cast<uv_handle_t *>(&socket),
                               &send_buffer_size) == 0;
}

bool Connection::Start() {
    SetDeadline(Deadline::IDENTIFICATION, initial_timeout);
    return StartReading();
}

void Connection::Identified() {
    SetDeadline(Deadline::READ, connection_timeout);
}

void Connection::BlockRequests() {
    requests_blocked = true;
    StopReading();
    SetDeadline(Deadline::BLOCKED, connection_timeout);
}

void Connection::StartResponse() {
    response_active = true;
    SetDeadline(Deadline::WRITE, connection_timeout);
}

void Connection::FinishResponse() {
    response_active = false;
    requests_blocked = false;
    if (IsClosing())
        return;
    SetDeadline(Deadline::READ, connection_timeout);
    StartReading();
}

void Connection::SetDeadline(Deadline reason, int seconds) {
    deadline = reason;
    if (!timer_initialized)
        return;
    uv_timer_stop(&timer);
    if (reason != Deadline::NONE && seconds > 0)
        uv_timer_start(&timer, TimerExpired,
                       static_cast<uint64_t>(seconds) * 1000, 0);
}

void Connection::ClearDeadline() {
    SetDeadline(Deadline::NONE, 0);
}

void Connection::Allocate(uv_handle_t *handle, size_t suggested_size,
                          uv_buf_t *buffer) {
    Connection *connection = static_cast<Connection *>(handle->data);
    *buffer = uv_buf_init(connection->incoming, sizeof connection->incoming);
}

void Connection::Read(uv_stream_t *stream, ssize_t length,
                      const uv_buf_t *buffer) {
    Connection *connection = static_cast<Connection *>(stream->data);
    if (length > 0) {
        if (connection->deadline == Deadline::READ)
            connection->SetDeadline(Deadline::READ,
                                    connection->connection_timeout);
        connection->Consume(buffer->base, static_cast<size_t>(length));
    } else if (length == UV_EOF) {
        connection->ReportReadFailure(ReadFailure::CLOSED);
    } else if (length < 0) {
        connection->ReportReadFailure(ReadFailure::IO_ERROR);
    }
}

bool Connection::StartReading() {
    if (reading || requests_blocked || IsClosing())
        return true;
    if (retained_size > 0) {
        size_t size = retained_size;
        retained_size = 0;
        Consume(retained_input, size);
        if (requests_blocked || IsClosing())
            return true;
    }
    int result = uv_read_start(GetStream(), Allocate, Read);
    if (result != 0)
        return false;
    reading = true;
    return true;
}

void Connection::StopReading() {
    if (!reading)
        return;
    uv_read_stop(GetStream());
    reading = false;
}

void Connection::ReportReadFailure(ReadFailure failure) {
    StopReading();
    try {
        read_failed(*this, failure);
    } catch (...) {
        Abort();
    }
}

void Connection::Consume(const char *data, size_t size) {
    size_t offset = 0;
    while (offset < size && !IsClosing()) {
        size_t consumed;
        RequestHeadParser::Result result =
                parser.Parse(data + offset, size - offset, &consumed);
        offset += consumed;
        if (deadline == Deadline::IDENTIFICATION &&
            parser.HasCompleteJPIPRequestLine())
            SetDeadline(Deadline::READ, connection_timeout);
        if (result == RequestHeadParser::INCOMPLETE)
            break;
        if (result == RequestHeadParser::MALFORMED) {
            ReportReadFailure(ReadFailure::MALFORMED);
            return;
        }
        if (result == RequestHeadParser::TOO_LARGE) {
            ReportReadFailure(ReadFailure::TOO_LARGE);
            return;
        }

        RequestHead request = parser.TakeRequest();
        Dispatch(std::move(request));
        if (requests_blocked || response_active) {
            StopReading();
            if (offset < size) {
                retained_size = min(size - offset, sizeof retained_input);
                copy(data + offset, data + offset + retained_size,
                     retained_input);
            }
            return;
        }
    }
}

void Connection::Dispatch(RequestHead &&request) {
    try {
        request_ready(*this, std::move(request));
    } catch (...) {
        Abort();
    }
}

void Connection::TimerExpired(uv_timer_t *timer) {
    Connection *connection = static_cast<Connection *>(timer->data);
    connection->deadline = Deadline::NONE;
    try {
        connection->deadline_reached(*connection);
    } catch (...) {
        connection->Abort();
    }
}

bool Connection::Send(Write *write, string data,
                      function<void(bool)> completed) {
    return Send(write, std::move(data), NULL, 0, string(),
                std::move(completed));
}

bool Connection::Send(Write *write, string first, const char *payload,
                      size_t payload_size, string last,
                      function<void(bool)> completed) {
    if (write->active || IsClosing())
        return false;
    write->connection = this;
    write->first = std::move(first);
    write->last = std::move(last);
    write->payload = payload;
    write->payload_size = payload_size;
    write->completed = std::move(completed);
    write->active = true;
    write->request.data = write;

    uv_buf_t buffers[3];
    unsigned int count = 0;
    if (!write->first.empty())
        buffers[count++] = uv_buf_init(&write->first[0], write->first.size());
    if (payload_size > 0)
        buffers[count++] = uv_buf_init(const_cast<char *>(payload), payload_size);
    if (!write->last.empty())
        buffers[count++] = uv_buf_init(&write->last[0], write->last.size());
    if (count == 0) {
        write->active = false;
        return false;
    }

    pending_writes++;
    int result = uv_write(&write->request, GetStream(), buffers, count,
                          WriteCompleted);
    if (result != 0) {
        pending_writes--;
        write->active = false;
        return false;
    }
    return true;
}

void Connection::WriteCompleted(uv_write_t *request, int status) {
    Write *write = static_cast<Write *>(request->data);
    write->connection->HandleWriteCompleted(write, status);
}

void Connection::HandleWriteCompleted(Write *write, int status) {
    pending_writes--;
    write->active = false;
    function<void(bool)> completed = std::move(write->completed);
    if (status == 0 && deadline == Deadline::WRITE)
        SetDeadline(Deadline::WRITE, connection_timeout);
    if (status != 0 && !IsClosing())
        Abort();
    if (completed) {
        try {
            completed(status == 0);
        } catch (...) {
            Abort();
        }
    }
    if (graceful_close && pending_writes == 0)
        StartShutdown();
    NotifyClosed();
}

void Connection::CloseGracefully() {
    if (IsClosing())
        return;
    graceful_close = true;
    requests_blocked = true;
    retained_size = 0;
    StopReading();
    if (pending_writes == 0) {
        ClearDeadline();
        StartShutdown();
    }
}

void Connection::StartShutdown() {
    if (shutdown_active || uv_is_closing(
            reinterpret_cast<uv_handle_t *>(&socket)))
        return;
    ClearDeadline();
    shutdown.data = this;
    shutdown_active = true;
    int result = uv_shutdown(&shutdown, GetStream(), ShutdownCompleted);
    if (result != 0) {
        shutdown_active = false;
        CloseHandles();
    }
}

void Connection::ShutdownCompleted(uv_shutdown_t *request, int status) {
    Connection *connection = static_cast<Connection *>(request->data);
    connection->shutdown_active = false;
    connection->CloseHandles();
}

void Connection::Abort() {
    if (IsClosing())
        return;
    requests_blocked = true;
    retained_size = 0;
    StopReading();
    ClearDeadline();
    CloseHandles();
}

void Connection::CloseHandles() {
    if (socket_initialized) {
        uv_handle_t *handle = reinterpret_cast<uv_handle_t *>(&socket);
        if (!uv_is_closing(handle))
            uv_close(handle, HandleClosed);
    }
    if (timer_initialized) {
        uv_handle_t *handle = reinterpret_cast<uv_handle_t *>(&timer);
        if (!uv_is_closing(handle))
            uv_close(handle, HandleClosed);
    }
}

void Connection::HandleClosed(uv_handle_t *handle) {
    Connection *connection = static_cast<Connection *>(handle->data);
    connection->open_handles--;
    connection->NotifyClosed();
}

void Connection::NotifyClosed() {
    if (!notified_closed && open_handles == 0 && pending_writes == 0 &&
        !shutdown_active) {
        notified_closed = true;
        try {
            closed(*this);
        } catch (...) {
        }
    }
}

bool Connection::IsClosing() const {
    return graceful_close || !socket_initialized ||
           uv_is_closing(reinterpret_cast<const uv_handle_t *>(&socket));
}

bool Connection::HasJPIPRoute() const {
    return parser.HasJPIPRoute();
}

const string &Connection::GetRequestTarget() const {
    return parser.GetTarget();
}

uv_stream_t *Connection::GetStream() {
    return reinterpret_cast<uv_stream_t *>(&socket);
}

}
