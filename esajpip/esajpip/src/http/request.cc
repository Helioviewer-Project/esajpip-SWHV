//#define SHOW_TRACES
#include "trace.h"
#include "request.h"

#define MAX_URI 1023

namespace http {

    void Request::ParseParameter(istream &stream, const string &param, string &value) {
        getline(stream, value, '&');
    }

    void Request::ParseParameters(istream &stream) {
        string param, value;

        parameters.clear();

        while (stream.good()) {
            value.clear();
            getline(stream, param, '=');
            ParseParameter(stream, param, value);
            if (stream) parameters[param] = value;
        }
    }

    bool Request::Parse(const string &line) {
        string cad, uri;

        type = Request::UNKNOWN;

        if (!line.empty()) {
            istringstream in(line);

            if (in >> cad >> uri >> protocol) {
                if (cad == "POST") type = Request::POST;
                else if (cad == "GET") type = Request::GET;

                if (type != Request::UNKNOWN) {
                    ParseURI(uri.substr(0, MAX_URI));
                    return true;
                }
            }
        }
        return false;
    }

    istream &operator>>(istream &in, Request &request) {
        string line;

        if (std::getline(in, line)) {
            TRACE("HTTP Request: " << line);

            if (!request.Parse(line))
                in.setstate(istream::failbit);
        }
        return in;
    }

    ostream &operator<<(ostream &out, const Request &request) {
        if (request.type != Request::UNKNOWN) {
            out << (request.type == Request::GET ? "GET" : "POST") << " " << request.object;

            if (!request.parameters.empty()) {
                out << "?";
                map<string, string>::const_iterator i = request.parameters.begin();

                if (!i->second.empty()) out << i->first << "=" << i->second;
                else out << i->first;

                while (++i != request.parameters.end()) {
                    out << "&";
                    if (!i->second.empty()) out << i->first << "=" << i->second;
                    else out << i->first;
                }
            }
            out << " " << request.protocol << Protocol::CRLF;
        }
        return out;
    }

}
