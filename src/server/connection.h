#ifndef _SERVER_CONNECTION_H_
#define _SERVER_CONNECTION_H_

#include <cstddef>
#include <functional>
#include <string>

#include <uv.h>

#include "http/request_head.h"

namespace server {

class Connection {
public:
    enum class ReadFailure {
        MALFORMED,
        TOO_LARGE,
        CLOSED,
        IO_ERROR
    };

    enum class Deadline {
        NONE,
        IDENTIFICATION,
        READ,
        WRITE,
        CHANNEL_WAIT
    };

    struct Write {
        uv_write_t request;
        Connection *connection = NULL;
        std::string first;
        std::string last;
        const char *payload = NULL;
        std::size_t payload_size = 0;
        std::function<void(bool)> completed;
        bool active = false;
    };

    typedef std::function<void(Connection &, http::RequestHead &&)> RequestReady;
    typedef std::function<void(Connection &, ReadFailure)> ReadFailed;
    typedef std::function<void(Connection &, Deadline)> DeadlineReached;
    typedef std::function<void(Connection &)> Closed;

private:
    uv_tcp_t socket;
    uv_timer_t timer;
    http::RequestHeadParser parser;
    RequestReady request_ready;
    ReadFailed read_failed;
    DeadlineReached deadline_reached;
    Closed closed;
    int initial_timeout;
    int connection_timeout;
    char incoming[4096];
    char retained_input[4096];
    std::size_t retained_size = 0;
    http::RequestHead retained_request;
    Deadline deadline = Deadline::NONE;
    std::size_t pending_writes = 0;
    int open_handles = 0;
    bool has_retained_request = false;
    bool requests_blocked = false;
    bool response_active = false;
    bool reading = false;
    bool graceful_close = false;
    bool shutdown_active = false;
    bool notified_closed = false;
    bool socket_initialized = false;
    bool timer_initialized = false;
    uv_shutdown_t shutdown;

    static void Allocate(uv_handle_t *handle, std::size_t suggested_size,
                         uv_buf_t *buffer);
    static void Read(uv_stream_t *stream, ssize_t length,
                     const uv_buf_t *buffer);
    static void TimerExpired(uv_timer_t *timer);
    static void WriteCompleted(uv_write_t *request, int status);
    static void ShutdownCompleted(uv_shutdown_t *request, int status);
    static void HandleClosed(uv_handle_t *handle);

    bool StartReading();
    void StopReading();
    void ReportReadFailure(ReadFailure failure);
    void Consume(const char *data, std::size_t size);
    void Dispatch(http::RequestHead &&request);
    void SetDeadline(Deadline reason, int seconds);
    void ClearDeadline();
    void StartShutdown();
    void CloseHandles();
    void HandleWriteCompleted(Write *write, int status);
    void NotifyClosed();
    uv_stream_t *GetStream();

public:
    Connection(int initial_timeout, int connection_timeout,
               RequestReady request_ready,
               ReadFailed read_failed, DeadlineReached deadline_reached,
               Closed closed);

    bool Initialize(uv_loop_t *loop);
    bool Accept(uv_stream_t *listener);
    bool Start();
    void Identified();
    void BlockRequests(Deadline reason = Deadline::CHANNEL_WAIT);
    void UnblockRequests();
    void StartResponse();
    void FinishResponse();
    // The payload must remain valid until the completion callback runs.
    bool Send(Write *write, std::string data,
              std::function<void(bool)> completed);
    bool Send(Write *write, std::string first, const char *payload,
              std::size_t payload_size, std::string last,
              std::function<void(bool)> completed);
    void CloseGracefully();
    void Abort();

    bool IsClosing() const;
    bool HasJPIPRoute() const;
    const std::string &GetRequestTarget() const;

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
};

}

#endif /* _SERVER_CONNECTION_H_ */
