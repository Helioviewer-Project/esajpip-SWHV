#ifndef _HTTP_RESPONSE_H_
#define _HTTP_RESPONSE_H_

#include <ostream>

#include "protocol.h"

namespace http {
    using namespace std;

    class Response {
    private:
        int code;
        const char *reason;
        Protocol protocol;

    public:
        Response(int _code, const char *_reason, const Protocol &_protocol = Protocol())
                : code(_code), reason(_reason), protocol(_protocol) {
        }

        friend ostream &operator<<(ostream &out, const Response &response) {
            return out << response.protocol << " " << response.code << " "
                       << response.reason << Protocol::CRLF;
        }
    };
}

#endif /* _HTTP_RESPONSE_H_ */
