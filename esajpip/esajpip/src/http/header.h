#ifndef _HTTP_HEADER_H_
#define _HTTP_HEADER_H_

#include <cstring>

#include <string>
#include <iostream>
#include <cassert>
#include "protocol.h"

namespace http {

    /**
     * Container for the strings associated to the most
     * common HTTP headers, used for the specialization of
     * the class <code>HeaderBase</code>.
     *
     * @see HeaderBase
     */
    class HeaderName {
    public:
        static const char UNDEFINED[];                     ///< No header name defined
        static const char CONTENT_TYPE[];                  ///< The header <code>Content-Type</code>
        static const char CACHE_CONTROL[];                 ///< The header <code>Cache-Control</code>
        static const char CONTENT_LENGTH[];                ///< The header <code>Content-Length</code>
        static const char ACCEPT_ENCODING[];               ///< The header <code>Accept-Encoding</code>
        static const char CONTENT_ENCODING[];              ///< The header <code>Content-Encoding</code>
        static const char TRANSFER_ENCODING[];             ///< The header <code>Transfer-Encoding</code>
        static const char ACCESS_CONTROL_ALLOW_ORIGIN[];   ///< The header <code>Access-Control-Allow-Origin</code>
        static const char ACCESS_CONTROL_EXPOSE_HEADERS[]; ///< The header <code>Access-Control-Expose-Headers</code>
        static const char STRICT_TRANSPORT_SECURITY[];     ///< The header <code>Strict-Transport-Security</code>
    };

    /**
     * Template class used to identify a HTTP header. It is
     * possible to use this class with standard streams. This
     * class is specialized with the header name.
     *
     * @see Header
     */
    template<const char *NAME>
    class HeaderBase {

    public:
        string value;   ///< String value of the header

        /**
         * Empty constructor.
         */
        HeaderBase() {
        }

        /**
         * Initializes the header value.
         */
        HeaderBase(const string &value) {
            this->value = value;
        }

        friend ostream &operator<<(ostream &out, const HeaderBase &header) {
            return out << NAME << ": " << header.value << Protocol::CRLF;
        }

        friend istream &operator>>(istream &in, HeaderBase &header) {
            assert(0);

            in.setstate(istream::failbit);
            return in;
        }

        /**
         * Returns the name of the header, used in the specialization
         * of the class.
         */
        static const char *name() {
            return NAME;
        }
    };

    /**
     * Specialization of the <code>HeaderBase</code> template class
     * with the <code>HeaderName::UNDEFINED</code> value. In this case
     * the header name is not fixed, handled by an internal variable.
     * This class is used as base for the class <code>Header</code>.
     *
     * @see Header
     */
    template<>
    class HeaderBase<HeaderName::UNDEFINED> {
    public:
        string name;    ///< Header name
        string value;   ///< Header value

        /**
         * Empty constructor.
         */
        HeaderBase() {
        }

        /**
         * Initializes the header content (name and value).
         * @param name Header name.
         * @param value Header value.
         */
        HeaderBase(const string &name, const string &value) {
            this->name = name;
            this->value = value;
        }

        friend ostream &operator<<(ostream &out, const HeaderBase &header) {
            return out << header.name << ": " << header.value << Protocol::CRLF;
        }

        friend istream &operator>>(istream &in, HeaderBase &header) {
            string line;

            if (getline(in, line)) {
                size_t line_size = line.size();
                if (line_size <= 0) in.setstate(istream::eofbit);
                else if ((line[0] == '\r') || (line[0] == '\n')) in.setstate(istream::eofbit);
                else {
                    size_t pos = line.find(':');

                    if (pos == string::npos) in.setstate(istream::failbit);
                    else {
                        header.name = line.substr(0, pos);

                        if ((pos += 2) >= line_size) in.setstate(istream::failbit);
                        else header.value = line.substr(pos, line_size - pos - 1);
                    }
                }
            }

            return in;
        }
    };

    /**
     * Class used to handle a HTTP header.
     *
     * @see HeaderBase
     * @see HeaderName
     */
    class Header : public HeaderBase<HeaderName::UNDEFINED> {
    public:
        /**
         * Predefined "Content-Type"
         */
        typedef HeaderBase<HeaderName::CONTENT_TYPE> ContentType;

        /**
         * Predefined "Cache-Control" header.
         */
        typedef HeaderBase<HeaderName::CACHE_CONTROL> CacheControl;

        /**
         * Predefined "Content-Length" header.
         */
        typedef HeaderBase<HeaderName::CONTENT_LENGTH> ContentLength;

        /**
         * Predefined "Accept-Encoding" header.
         */
        typedef HeaderBase<HeaderName::ACCEPT_ENCODING> AcceptEncoding;

        /**
         * Predefined "Content-Encoding" header.
         */
        typedef HeaderBase<HeaderName::CONTENT_ENCODING> ContentEncoding;

        /**
         * Predefined "Transfer-Encoding" header.
         */
        typedef HeaderBase<HeaderName::TRANSFER_ENCODING> TransferEncoding;

        /**
         * Predefined "Access-Control-Allow-Origin" header.
         */
        typedef HeaderBase<HeaderName::ACCESS_CONTROL_ALLOW_ORIGIN> AccessControlAllowOrigin;

        /**
         * Predefined "Access-Control-Expose-Headers" header.
         */
        typedef HeaderBase<HeaderName::ACCESS_CONTROL_EXPOSE_HEADERS> AccessControlExposeHeaders;

        /**
         * Predefined "Strict-Transport-Security" header.
         */
        typedef HeaderBase<HeaderName::STRICT_TRANSPORT_SECURITY> StrictTransportSecurity;

        /**
         * Empty constructor.
         */
        Header() : HeaderBase<HeaderName::UNDEFINED>() {
        }

        /**
         * Initializes the header content (name and value).
         * @param name Header name.
         * @param value Header value.
         */
        Header(const string &name, const string &value) : HeaderBase<HeaderName::UNDEFINED>(name, value) {
        }

        /**
         * Returns <code>true</code> if the names of the two headers are equal.
         */
        template<const char *NAME>
        friend bool operator==(const Header &a, const HeaderBase<NAME> &b) {
            return (strcasecmp(a.name.c_str(), b.name()) == 0);
        }
    };
}

#endif /* _HTTP_HEADER_H_ */
