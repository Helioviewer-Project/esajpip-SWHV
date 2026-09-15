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

    public:
        Response(int _code, const char *_reason) : code(_code), reason(_reason) {
        }

        friend ostream &operator<<(ostream &out, const Response &response) {
            return out << "HTTP/1.1 " << response.code << " " << response.reason << CRLF;
        }
    };
}

#endif /* _HTTP_RESPONSE_H_ */
