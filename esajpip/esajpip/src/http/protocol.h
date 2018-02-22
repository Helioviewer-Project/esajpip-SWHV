#ifndef _HTTP_PROTOCOL_H_
#define _HTTP_PROTOCOL_H_

#include <cassert>
#include <iostream>
#include <string>

namespace http {
    using namespace std;

    /**
     * Class used to identify the HTTP protocol. It is possible
     * to use this class with standard streams.
     */
    class Protocol {
    private:
        int majorVersion_;    ///< major protocol version
        int minorVersion_;    ///< minor protocol version

    public:
        /**
         * String with the characters 13 (CR) and 10 (LF),
         * the line separator used in the HTTP protocol.
         */
        static const char CRLF[];

        /**
         * Initialized the protocl with the given version. By
         * default the version is 1.1.
         * @param majorVersion major protocol version
         * @param minorVersion Minor protocol version
         */
        Protocol(int majorVersion = 1, int minorVersion = 1) {
            assert(((majorVersion == 1) && (minorVersion == 0)) ||
                   ((majorVersion == 1) && (minorVersion == 1)));

            majorVersion_ = majorVersion;
            minorVersion_ = minorVersion;
        }

        /**
         * Copy constructor.
         */
        Protocol(const Protocol &protocol) {
            majorVersion_ = protocol.majorVersion_;
            minorVersion_ = protocol.minorVersion_;
        }

        friend ostream &operator<<(ostream &out, const Protocol &protocol) {
            return out << "HTTP/" << protocol.majorVersion_ << "." << protocol.minorVersion_;
        }

        friend istream &operator>>(istream &in, Protocol &protocol) {
            string cad;

            in >> cad;
            if (!cad.compare(0, 8, "HTTP/1.0")) protocol = Protocol(1, 0);
            else if (!cad.compare(0, 8, "HTTP/1.1")) protocol = Protocol(1, 1);
            else in.setstate(istream::failbit);

            return in;
        }

        /**
         * Returns the major number of the protocol version.
         */
/*
        int majorVersion() const {
            return majorVersion_;
        }
*/

        /**
         * Returns the minor number of the protocol version.
         */
/*
        int minorVersion() const {
            return minorVersion_;
        }
*/
    };
}

#endif /* _HTTP_PROTOCOL_H_ */
