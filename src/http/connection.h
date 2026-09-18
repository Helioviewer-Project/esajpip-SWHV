#ifndef _HTTP_CONNECTION_H_
#define _HTTP_CONNECTION_H_

#include <cstddef>
#include <string>

namespace http {

std::string EscapeForLog(const std::string &text);

struct RequestHead {
    std::string line;
    bool accepts_gzip;
    bool close;
    bool unsupported_body;
};

class Connection {
public:
    enum ReadResult {
        REQUEST_READY,
        CONNECTION_CLOSED,
        INTERRUPTED,
        TIMED_OUT,
        REQUEST_TOO_LARGE,
        READ_FAILED
    };

private:
    // The channel owns and closes fd; this class only performs HTTP I/O on it.
    int fd;
    int interrupt_fd;
    int timeout_ms;
    int timeout_seconds;
    char buf[1024];
    size_t pos;
    size_t len;

    enum LineResult {
        LINE_READY,
        LINE_CLOSED,
        LINE_INCOMPLETE,
        LINE_INTERRUPTED,
        LINE_TIMED_OUT,
        LINE_TOO_LARGE,
        LINE_FAILED
    };

    LineResult ReadLine(std::string &line, size_t &remaining);

public:
    Connection(int _fd, int _interrupt_fd, int timeout_seconds);

    bool Configure();
    ReadResult ReadRequestHead(RequestHead *request);
    bool Send(const void *data, size_t length);
    bool Send(const std::string &data);
    bool SendOK(const std::string &headers);
    bool SendChunk(const void *data, size_t length);

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
};

}

#endif /* _HTTP_CONNECTION_H_ */
