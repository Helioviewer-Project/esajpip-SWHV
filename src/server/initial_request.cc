#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include "initial_request.h"

using namespace std;

static const size_t MAX_REQUEST_LINE = 2048;

static bool GetParameter(const char *begin, const char *end, const char *name,
                         size_t name_length, string *value) {
    while (begin < end) {
        const char *separator = static_cast<const char *>(memchr(begin, '&', end - begin));
        const char *parameter_end = separator ? separator : end;
        const char *equals = static_cast<const char *>(memchr(begin, '=', parameter_end - begin));
        const char *parameter_name_end = equals ? equals : parameter_end;
        if (static_cast<size_t>(parameter_name_end - begin) == name_length &&
            memcmp(begin, name, name_length) == 0) {
            value->assign(equals ? equals + 1 : parameter_end, parameter_end);
            return true;
        }
        if (!separator)
            break;
        begin = separator + 1;
    }
    return false;
}

InitialRequest InspectInitialRequest(int fd) {
    char line[MAX_REQUEST_LINE];
    ssize_t length;
    do {
        length = recv(fd, line, sizeof line, MSG_PEEK | MSG_DONTWAIT);
    } while (length < 0 && errno == EINTR);

    if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return {REQUEST_PENDING, false, ""};
    if (length <= 0)
        return {REQUEST_REJECTED, false, ""};

    static const char method[] = "GET ";
    size_t prefix_length = min(static_cast<size_t>(length), sizeof method - 1);
    if (memcmp(line, method, prefix_length) != 0)
        return {REQUEST_REJECTED, false, ""};
    if (static_cast<size_t>(length) < sizeof method - 1)
        return {REQUEST_PENDING, false, ""};

    const char *newline = static_cast<const char *>(memchr(line, '\n', length));
    if (newline == NULL)
        return {static_cast<size_t>(length) == sizeof line ? REQUEST_REJECTED : REQUEST_PENDING,
                false, ""};

    const char *uri = line + sizeof method - 1;
    const char *uri_end = static_cast<const char *>(memchr(uri, ' ', newline - uri));
    if (!uri_end)
        return {REQUEST_REJECTED, false, ""};

    const char *protocol = uri_end;
    while (protocol < newline && *protocol == ' ')
        ++protocol;
    if (newline - protocol < 8 ||
        (memcmp(protocol, "HTTP/1.0", 8) != 0 && memcmp(protocol, "HTTP/1.1", 8) != 0))
        return {REQUEST_REJECTED, false, ""};

    const char *query = static_cast<const char *>(memchr(uri, '?', uri_end - uri));
    if (!query)
        return {REQUEST_REJECTED, false, ""};

    string cid;
    string cclose;
    string cnew;
    bool has_cid = GetParameter(query + 1, uri_end, "cid", 3, &cid);
    bool has_close = GetParameter(query + 1, uri_end, "cclose", 6, &cclose);
    bool has_new = GetParameter(query + 1, uri_end, "cnew", 4, &cnew);

    if (has_close) {
        if (cclose == "*" && has_cid)
            cclose = cid;
        return cclose.empty() || cclose == "*"
                   ? InitialRequest{REQUEST_REJECTED, false, ""}
                   : InitialRequest{REQUEST_ACCEPTED, false, cclose};
    }
    if (has_new)
        return {REQUEST_ACCEPTED, true, ""};
    if (has_cid && !cid.empty())
        return {REQUEST_ACCEPTED, false, cid};
    return {REQUEST_REJECTED, false, ""};
}
