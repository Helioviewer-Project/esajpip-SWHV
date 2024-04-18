#ifndef _HTTP_REQUEST_H_
#define _HTTP_REQUEST_H_

#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include "header.h"
#include "protocol.h"

namespace http {
    using namespace std;

    /**
     * Class used to identify a HTTP request (GET or POST). It
     * is possible to use this class with standard streams.
     *
     * @see Response
     */
    class Request {
    public:
        /**
         * Request type enumeration.
         */
        enum Type {
            GET,
            UNKNOWN
        };

        Type type;            ///< Request type (GET or POST)
        string object;        ///< Object associated to the request
        Protocol protocol;    ///< Protocol version used

        /**
         * Map with all the parameters when using the CGI form.
         */
        map<string, string> parameters;

        /**
         * Initializes the request.
         * @param type Request type (GET by default).
         * @param uri Request URI ("/" by default).
         * @param protocol Protocol version (1.1 by default).
         */
        Request(Type type = Request::GET, const string &uri = "/", const Protocol &protocol = Protocol(1, 1)) {
            this->type = type;
            this->protocol = protocol;

            ParseURI(uri);
        }

        /**
         * Parses a request from a string.
         * @param line String that contains the request to parse.
         * @return <code>true</code> if successful.
         */
        bool Parse(const string &line);

        /**
         * Parses a URI from a string.
         * @param uri String that contains the URI to parse.
         */
        void ParseURI(const string &uri) {
            istringstream uri_str(uri);
            getline(uri_str, object, '?');
            ParseParameters(uri_str);
        }

        /**
         * Parses the parameters from an input stream.
         * @param stream Input stream.
         */
        virtual void ParseParameters(istream &stream);

        /**
         * Parses one parameter from an input stream.
         * @param stream Input stream.
         * @param param Parameter name.
         * @param value Parameter value.
         */
        virtual void ParseParameter(istream &stream, const string &param, string &value);

        friend istream &operator>>(istream &in, Request &request);

        friend ostream &operator<<(ostream &out, const Request &request);
    };
}

#endif /* _HTTP_REQUEST_H_ */
