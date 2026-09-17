#include "trace.h"
#include "http/connection.h"
#include "http/header.h"
#include "http/protocol.h"

#include <glib.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

using namespace std;

namespace {

const size_t MAX_REQUEST_HEAD = 4096;
const int SEND_BUFFER_SIZE = 524288;
const int ENABLED = 1;

int TimeoutMilliseconds(int seconds) {
    if (seconds <= 0)
        return -1;
    if (seconds > INT_MAX / 1000)
        return INT_MAX;
    return seconds * 1000;
}

bool SendBuffers(int fd, iovec *buffers, int count) {
    while (count > 0) {
        ssize_t sent = writev(fd, buffers, count);
        if (sent < 0) {
            if (errno == EINTR)
                continue;
            ERROR("Could not send: " << strerror(errno));
            return false;
        }
        if (sent == 0) {
            ERROR("Could not send: connection closed");
            return false;
        }

        while (count > 0 && sent >= static_cast<ssize_t>(buffers->iov_len)) {
            sent -= buffers->iov_len;
            buffers++;
            count--;
        }
        if (sent > 0) {
            buffers->iov_base = static_cast<char *>(buffers->iov_base) + sent;
            buffers->iov_len -= sent;
        }
    }
    return true;
}

}

namespace http {

string EscapeForLog(const string &text) {
    char *escaped = g_strescape(text.c_str(), NULL);
    string result(escaped);
    g_free(escaped);
    return result;
}

Connection::Connection(int _fd, int _interrupt_fd, int timeout_seconds)
    : fd(_fd), interrupt_fd(_interrupt_fd),
      timeout_ms(TimeoutMilliseconds(timeout_seconds)),
      timeout_seconds(timeout_seconds), pos(0), len(0) {
}

bool Connection::Configure() {
    int result = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &SEND_BUFFER_SIZE,
                            sizeof SEND_BUFFER_SIZE) |
                 setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &ENABLED,
                            sizeof ENABLED);
    if (result == 0 && timeout_seconds > 0) {
        timeval value;
        value.tv_sec = timeout_seconds;
        value.tv_usec = 0;
        result |= setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof value) |
                  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof value);
    }
    if (result == 0)
        return true;
    LOG("setsockopt failed: " << strerror(errno));
    return false;
}

Connection::LineResult Connection::ReadLine(string &line, size_t &remaining) {
    line.clear();
    for (;;) {
        const char *newline = static_cast<const char *>(
                memchr(buf + pos, '\n', len - pos));
        size_t part_length = newline != NULL ? newline - (buf + pos) : len - pos;
        size_t consumed = part_length + (newline != NULL);
        if (consumed > remaining) {
            errno = EMSGSIZE;
            return LINE_FAILED;
        }
        line.append(buf + pos, part_length);
        remaining -= consumed;
        if (newline != NULL) {
            pos = newline - buf + 1;
            return LINE_READY;
        }

        pollfd fds[] = {
            {fd, POLLIN, 0},
            {interrupt_fd, POLLIN, 0}
        };
        int ready;
        do {
            ready = poll(fds, 2, timeout_ms);
        } while (ready < 0 && errno == EINTR);
        if (ready < 0)
            return LINE_FAILED;
        if (ready == 0)
            return LINE_TIMED_OUT;
        if (fds[1].revents & POLLIN)
            return LINE_INTERRUPTED;
        if (!fds[0].revents) {
            errno = EIO;
            return LINE_FAILED;
        }

        ssize_t received;
        do {
            received = recv(fd, buf, sizeof buf, MSG_DONTWAIT);
        } while (received < 0 && errno == EINTR);
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (received <= 0) {
            if (received < 0)
                return LINE_FAILED;
            return line.empty() ? LINE_CLOSED : LINE_INCOMPLETE;
        }
        pos = 0;
        len = received;
    }
}

Connection::ReadResult Connection::ReadRequestHead(RequestHead *request) {
    request->line.clear();
    request->accepts_gzip = false;
    size_t remaining = MAX_REQUEST_HEAD;
    LineResult result = ReadLine(request->line, remaining);
    if (result == LINE_INTERRUPTED)
        return INTERRUPTED;
    if (result == LINE_TIMED_OUT)
        return TIMED_OUT;
    if (result == LINE_CLOSED)
        return CONNECTION_CLOSED;
    if (result != LINE_READY) {
        if (result == LINE_FAILED)
            LOG("Request read error: " << strerror(errno));
        else
            LOG("Incomplete request line: " << EscapeForLog(request->line));
        return READ_FAILED;
    }
    if (!request->line.empty() && request->line.back() == '\r')
        request->line.pop_back();

    Header header;
    string line;
    for (;;) {
        result = ReadLine(line, remaining);
        if (result == LINE_INTERRUPTED)
            return INTERRUPTED;
        if (result == LINE_TIMED_OUT)
            return TIMED_OUT;
        if (result != LINE_READY) {
            if (result == LINE_FAILED)
                LOG("Header read error: " << strerror(errno));
            else if (result == LINE_INCOMPLETE)
                LOG("Incomplete HTTP header");
            return READ_FAILED;
        }
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            return REQUEST_READY;
        if (!header.Parse(line)) {
            LOG("Invalid HTTP header");
            return READ_FAILED;
        }
        if (header.Is("Accept-Encoding") &&
            header.value.find("gzip") != string::npos)
            request->accepts_gzip = true;
    }
}

bool Connection::Send(const void *data, size_t length) {
    iovec buffer = {const_cast<void *>(data), length};
    return SendBuffers(fd, &buffer, 1);
}

bool Connection::Send(const string &data) {
    return Send(data.data(), data.size());
}

bool Connection::SendOK(const string &headers) {
    static const char status[] = "HTTP/1.1 200 OK\r\n";
    iovec buffers[] = {
        {const_cast<char *>(status), sizeof status - 1},
        {const_cast<char *>(headers.data()), headers.size()},
        {const_cast<char *>(CRLF), sizeof CRLF - 1}
    };
    return SendBuffers(fd, buffers, 3);
}

bool Connection::SendChunk(const void *data, size_t length) {
    if (length == 0)
        return true;

    char header[2 * sizeof(size_t) + 3];
    int header_length = snprintf(header, sizeof header, "%zx\r\n", length);
    iovec buffers[] = {
        {header, static_cast<size_t>(header_length)},
        {const_cast<void *>(data), length},
        {const_cast<char *>(CRLF), sizeof CRLF - 1}
    };
    return SendBuffers(fd, buffers, 3);
}

}
