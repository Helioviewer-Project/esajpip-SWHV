#ifndef _HTTP_PROTOCOL_H_
#define _HTTP_PROTOCOL_H_

#include <cstddef>

namespace http {
    constexpr char CRLF[] = "\r\n";
    constexpr std::size_t MAX_INITIAL_REQUEST_LINE = 2048;
    constexpr std::size_t MAX_REQUEST_HEAD = 4096;
}

#endif /* _HTTP_PROTOCOL_H_ */
