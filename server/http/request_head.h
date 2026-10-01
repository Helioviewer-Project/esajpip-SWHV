#pragma once

#include <cstddef>
#include <string>

#include <llhttp.h>

namespace server {

struct RequestHead {
    std::string target;
    bool accepts_gzip = false;
    bool close = false;
    bool unsupported_body = false;
};

class RequestHeadParser {
public:
    enum Result {
        INCOMPLETE,
        COMPLETE,
        MALFORMED,
        TOO_LARGE
    };

private:
    llhttp_t parser;
    RequestHead request;
    std::string header_name;
    std::string header_value;
    std::size_t head_size = 0;
    std::size_t line_size = 0;
    bool line_complete = false;
    int host_count = 0;
    int content_length_count = 0;
    bool malformed = false;

    static int ReadTarget(llhttp_t *parser, const char *data,
                          std::size_t length) noexcept;
    static int ReadHeaderName(llhttp_t *parser, const char *data,
                              std::size_t length) noexcept;
    static int ReadHeaderValue(llhttp_t *parser, const char *data,
                               std::size_t length) noexcept;
    static int FinishHeader(llhttp_t *parser) noexcept;
    static int FinishHead(llhttp_t *parser) noexcept;

    void ProcessHeader();
    void CountBytes(const char *data, std::size_t length);
    void Reset();

public:
    RequestHeadParser();

    Result Parse(const char *data, std::size_t length, std::size_t *consumed);
    bool HasCompleteJPIPRequestLine();
    bool HasJPIPRoute() const;
    const std::string &GetTarget() const;
    RequestHead TakeRequest();

    RequestHeadParser(const RequestHeadParser &) = delete;
    RequestHeadParser &operator=(const RequestHeadParser &) = delete;
    RequestHeadParser(RequestHeadParser &&) = delete;
    RequestHeadParser &operator=(RequestHeadParser &&) = delete;
};

}
