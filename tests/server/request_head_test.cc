#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>

#include "jpip/request/request.h"
#include "server/http/request_head.h"

using namespace std;
using server::RequestHeadParser;

static void Check(bool condition, const char *message) {
    if (!condition) { cerr << message << '\n'; exit(EXIT_FAILURE); }
}

void CheckRequestSyntax() {
    const char *invalid[] = {
        "GET /movie.jpx?cnew=http HTTP/1.0\r\nHost: localhost\r\n\r\n",
        "GET /movie.jpx?cnew=http HTTP/1.1junk\r\nHost: localhost\r\n\r\n",
        "GET /jpip?cid=7 HTTP/1.1 trailing\r\nHost: localhost\r\n\r\n"
    };
    for (const char *head : invalid) {
        server::RequestHeadParser parser;
        size_t consumed;
        Check(parser.Parse(head, strlen(head), &consumed) ==
                      server::RequestHeadParser::MALFORMED,
              "The HTTP parser accepted an invalid request line");
    }
}

void CheckLongRequestTarget() {
    const string prefix = "/image.jp2?padding=";
    const size_t route_offsets[] = {1000, 1037};
    for (size_t i = 0; i < 2; ++i) {
        string target = prefix +
                string(route_offsets[i] - prefix.size(), 'x') + "&cnew=http";
        string head = "GET " + target + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
        server::RequestHeadParser parser;
        size_t consumed;
        Check(parser.Parse(head.data(), head.size(), &consumed) ==
                      server::RequestHeadParser::COMPLETE,
              "Could not parse the routing-limit request");

        jpip::Request request;
        Check(request.ParseTarget(target),
              "Could not fully parse the routing-limit target");
        Check(parser.HasJPIPRoute() && request.routing.cnew,
              "A bounded request target was truncated before JPIP parsing");
    }
}

void CheckRouteClassification() {
    server::RequestHeadParser parser;
    size_t consumed;
    const string routed =
            "GET /image.jp2?cnew=http HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Check(parser.Parse(routed.data(), routed.size(), &consumed) ==
                  server::RequestHeadParser::COMPLETE &&
                  parser.HasJPIPRoute(),
          "Could not classify a complete JPIP request line");
    parser.TakeRequest();

    const string unrelated =
            "GET /status HTTP/1.1\r\nHost: localhost\r\n\r\n";
    Check(parser.Parse(unrelated.data(), unrelated.size(), &consumed) ==
                  server::RequestHeadParser::COMPLETE &&
                  !parser.HasJPIPRoute(),
          "JPIP route classification survived parser reset");

    server::RequestHeadParser partial_parser;
    const string oversized = "GET /image.jp2?cnew=http&padding=" +
            string(2050, 'x');
    Check(partial_parser.Parse(oversized.data(), oversized.size(), &consumed) ==
                  server::RequestHeadParser::TOO_LARGE &&
                  partial_parser.HasJPIPRoute(),
          "An oversized partial JPIP request lost route identification");
}


static void CheckSplitPoints() {
    string target = "/jpip?cid=7&fsiz=512,512";
    string head = "GET " + target + " HTTP/1.1\r\nHost: localhost\r\n"
            "Accept-Encoding: gzip\r\nConnection: keep-alive, ClOsE\r\n"
            "Content-Length: 0\r\n\r\n";
    string next = "GET /status HTTP/1.1\r\nHost: localhost\r\n\r\n";
    for (size_t split = 1; split < head.size(); ++split) {
        RequestHeadParser parser;
        size_t consumed = 0;
        Check(parser.Parse(head.data(), split, &consumed) == RequestHeadParser::INCOMPLETE &&
                      consumed == split, "Fragment was not consumed exactly");
        string suffix = head.substr(split) + next;
        Check(parser.Parse(suffix.data(), suffix.size(), &consumed) == RequestHeadParser::COMPLETE &&
                      consumed == head.size() - split && parser.HasJPIPRoute(),
              "Split parser consumed the following request or lost routing");
        server::RequestHead request = parser.TakeRequest();
        Check(request.target == target && request.accepts_gzip && request.close &&
                      !request.unsupported_body, "Split parsing changed head fields");
        Check(parser.Parse(next.data(), next.size(), &consumed) == RequestHeadParser::COMPLETE &&
                      consumed == next.size() && !parser.HasJPIPRoute(),
              "Parser did not reset for pipelined request");
        request = parser.TakeRequest();
        Check(request.target == "/status" && !request.accepts_gzip && !request.close &&
                      !request.unsupported_body, "Request flags survived parser reset");
    }
    RequestHeadParser parser;
    for (size_t at = 0; at < head.size(); ++at) {
        size_t consumed = 0;
        Check(parser.Parse(head.data() + at, 1, &consumed) ==
                      (at + 1 == head.size() ? RequestHeadParser::COMPLETE : RequestHeadParser::INCOMPLETE) &&
                      consumed == 1, "Bytewise parsing changed completion boundary");
    }
}

static void CheckHeaders() {
    for (const char *headers : {"", "Host: \r\n", "Host: a\r\nHost: b\r\n",
                               "Host: a\r\nContent-Length: 0\r\nContent-Length: 0\r\n",
                               "Host: a\r\nContent-Length: 00\r\n"}) {
        string head = string("GET /jpip?cid=7 HTTP/1.1\r\n") + headers + "\r\n";
        RequestHeadParser parser;
        size_t consumed;
        Check(parser.Parse(head.data(), head.size(), &consumed) == RequestHeadParser::MALFORMED,
              "Invalid Host or Content-Length was accepted");
    }
    for (const char *header : {"Content-Length: 1\r\n", "Transfer-Encoding: chunked\r\n"}) {
        string head = string("GET /jpip?cid=7 HTTP/1.1\r\nHost: a\r\n") + header + "\r\n";
        RequestHeadParser parser;
        size_t consumed;
        Check(parser.Parse(head.data(), head.size(), &consumed) == RequestHeadParser::COMPLETE &&
                      parser.TakeRequest().unsupported_body,
              "Body-bearing head did not retain unsupported-body flag");
        const string clean = "GET /status HTTP/1.1\r\nHost: a\r\n\r\n";
        Check(parser.Parse(clean.data(), clean.size(), &consumed) == RequestHeadParser::COMPLETE &&
                      !parser.TakeRequest().unsupported_body,
              "Unsupported-body flag survived reset");
    }
    for (const char *line : {"POST /jpip?cid=7 HTTP/1.1\r\n", "GET /jpip?cid=7 HTTP/2.0\r\n"}) {
        string head = string(line) + "Host: a\r\n\r\n";
        RequestHeadParser parser;
        size_t consumed;
        Check(parser.Parse(head.data(), head.size(), &consumed) == RequestHeadParser::MALFORMED,
              "Unsupported method or version was accepted");
    }
}

static void CheckLimits() {
    for (size_t size : {size_t(2047), size_t(2048), size_t(2049)}) {
        string line = "GET /jpip?cid=7&pad=";
        line += string(size - line.size() - strlen(" HTTP/1.1\r\n"), 'x') + " HTTP/1.1\r\n";
        string head = line + "Host: a\r\n\r\n";
        RequestHeadParser parser;
        size_t consumed;
        Check(parser.Parse(head.data(), head.size(), &consumed) ==
                      (size <= 2048 ? RequestHeadParser::COMPLETE : RequestHeadParser::TOO_LARGE),
              "Request-line limit has the wrong boundary");
    }
    for (size_t size : {size_t(4095), size_t(4096), size_t(4097)}) {
        string prefix = "GET /jpip?cid=7 HTTP/1.1\r\nHost: a\r\nX-Pad: ";
        string head = prefix + string(size - prefix.size() - 4, 'x') + "\r\n\r\n";
        for (bool bytewise : {false, true}) {
            RequestHeadParser parser;
            size_t consumed;
            RequestHeadParser::Result result = RequestHeadParser::INCOMPLETE;
            for (size_t at = 0; at < head.size() && result == RequestHeadParser::INCOMPLETE;) {
                size_t n = bytewise ? 1 : head.size();
                result = parser.Parse(head.data() + at, n, &consumed);
                at += consumed;
            }
            Check(result == (size <= 4096 ? RequestHeadParser::COMPLETE : RequestHeadParser::TOO_LARGE),
                  "Head-size limit has the wrong boundary");
        }
    }
    RequestHeadParser parser;
    string partial = "GET /jpip?cid=7&pad=";
    partial += string(2048 - partial.size(), 'x');
    size_t consumed;
    Check(parser.Parse(partial.data(), partial.size(), &consumed) == RequestHeadParser::TOO_LARGE,
          "Unterminated request line bypassed its size limit");
}

int main() {
    CheckRequestSyntax();
    CheckLongRequestTarget();
    CheckRouteClassification();
    CheckSplitPoints();
    CheckHeaders();
    CheckLimits();
    return EXIT_SUCCESS;
}
