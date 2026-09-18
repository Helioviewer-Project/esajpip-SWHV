#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include "channel_id.h"
#include "http/protocol.h"
#include "initial_request.h"
#include "jpip/query.h"

using namespace std;

InitialRequest InspectInitialRequest(int fd) {
    char line[http::MAX_INITIAL_REQUEST_LINE];
    ssize_t length;
    do {
        length = recv(fd, line, sizeof line, MSG_PEEK | MSG_DONTWAIT);
    } while (length < 0 && errno == EINTR);

    if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return {REQUEST_PENDING, false, false, false, ""};
    if (length <= 0)
        return {REQUEST_REJECTED, false, false, false, ""};

    return ClassifyInitialRequest(line, length);
}

InitialRequest ClassifyInitialRequest(const char *line, size_t length) {
    if (length == 0)
        return {REQUEST_PENDING, false, false, false, ""};

    length = min(length, static_cast<size_t>(http::MAX_INITIAL_REQUEST_LINE));

    static const char method[] = "GET ";
    size_t prefix_length = min(length, sizeof method - 1);
    if (memcmp(line, method, prefix_length) != 0)
        return {REQUEST_REJECTED, false, false, false, ""};
    if (length < sizeof method - 1)
        return {REQUEST_PENDING, false, false, false, ""};

    const char *newline = static_cast<const char *>(memchr(line, '\n', length));
    if (newline == NULL)
        return {length == http::MAX_INITIAL_REQUEST_LINE ? REQUEST_REJECTED : REQUEST_PENDING,
                false, false, false, ""};

    const char *uri = line + sizeof method - 1;
    const char *uri_end = static_cast<const char *>(memchr(uri, ' ', newline - uri));
    if (!uri_end)
        return {REQUEST_REJECTED, false, false, false, ""};

    const char *protocol = uri_end;
    while (protocol < newline && *protocol == ' ')
        ++protocol;
    size_t protocol_length = newline - protocol;
    if (protocol_length > 0 && protocol[protocol_length - 1] == '\r')
        --protocol_length;
    if (protocol_length != 8 || memcmp(protocol, "HTTP/1.1", 8) != 0)
        return {REQUEST_REJECTED, false, false, false, ""};

    const char *parsed_uri_end = min(uri_end, uri + jpip::MAX_URI_LENGTH);
    const char *query = static_cast<const char *>(memchr(uri, '?', parsed_uri_end - uri));
    if (!query)
        return {REQUEST_REJECTED, false, false, false, ""};

    jpip::Query parameters = jpip::ParseQuery(query + 1, parsed_uri_end);
    const string *cid = jpip::FindParameter(parameters, "cid");
    const string *cclose = jpip::FindParameter(parameters, "cclose");
    const string *cnew = jpip::FindParameter(parameters, "cnew");
    bool tid = jpip::FindParameter(parameters, "tid") != NULL;
    bool handled = jpip::FindParameter(parameters, "handled") != NULL;

    if (cclose) {
        return IsChannelId(*cclose)
                   ? InitialRequest{REQUEST_ACCEPTED, false, tid, handled, *cclose}
                   : InitialRequest{REQUEST_REJECTED, false, tid, handled, ""};
    }
    if (cnew)
        return {REQUEST_ACCEPTED, true, tid, handled, ""};
    if (cid && IsChannelId(*cid))
        return {REQUEST_ACCEPTED, false, tid, handled, *cid};
    return {REQUEST_REJECTED, false, tid, handled, ""};
}
