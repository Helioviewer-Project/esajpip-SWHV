#include "server/request_head.h"

#include <algorithm>
#include <cstring>
#include <strings.h>
#include <utility>

#include "jpip/query.h"

using namespace std;

namespace {

const size_t MAX_INITIAL_REQUEST_LINE = 2048;
const size_t MAX_REQUEST_HEAD = 4096;

bool HasToken(const string &value, const char *token) {
    size_t position = 0;
    while (position < value.size()) {
        while (position < value.size() &&
               (value[position] == ' ' || value[position] == '\t' ||
                value[position] == ','))
            position++;
        size_t end = value.find(',', position);
        if (end == string::npos)
            end = value.size();
        size_t token_end = end;
        while (token_end > position &&
               (value[token_end - 1] == ' ' || value[token_end - 1] == '\t'))
            token_end--;
        if (token_end - position == strlen(token) &&
            strncasecmp(value.data() + position, token, strlen(token)) == 0)
            return true;
        position = end + 1;
    }
    return false;
}

string Trim(const string &value) {
    size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t'))
        begin++;
    size_t end = value.size();
    while (end > begin &&
           (value[end - 1] == ' ' || value[end - 1] == '\t'))
        end--;
    return value.substr(begin, end - begin);
}

}

namespace server {

RequestHeadParser::RequestHeadParser() {
    llhttp_settings_init(&settings);
    settings.on_url = ReadTarget;
    settings.on_header_field = ReadHeaderName;
    settings.on_header_value = ReadHeaderValue;
    settings.on_header_value_complete = FinishHeader;
    settings.on_headers_complete = FinishHead;
    Reset();
}

void RequestHeadParser::Reset() {
    request = RequestHead();
    header_name.clear();
    header_value.clear();
    head_size = 0;
    line_size = 0;
    line_complete = false;
    route_checked = false;
    route_present = false;
    host_count = 0;
    content_length_count = 0;
    malformed = false;
    llhttp_init(&parser, HTTP_REQUEST, &settings);
    parser.data = this;
}

int RequestHeadParser::ReadTarget(llhttp_t *parser, const char *data,
                                  size_t length) noexcept {
    RequestHeadParser *self = static_cast<RequestHeadParser *>(parser->data);
    self->request.target.append(data, length);
    return HPE_OK;
}

int RequestHeadParser::ReadHeaderName(llhttp_t *parser, const char *data,
                                      size_t length) noexcept {
    RequestHeadParser *self = static_cast<RequestHeadParser *>(parser->data);
    self->header_name.append(data, length);
    return HPE_OK;
}

int RequestHeadParser::ReadHeaderValue(llhttp_t *parser, const char *data,
                                       size_t length) noexcept {
    RequestHeadParser *self = static_cast<RequestHeadParser *>(parser->data);
    self->header_value.append(data, length);
    return HPE_OK;
}

int RequestHeadParser::FinishHeader(llhttp_t *parser) noexcept {
    RequestHeadParser *self = static_cast<RequestHeadParser *>(parser->data);
    self->ProcessHeader();
    self->header_name.clear();
    self->header_value.clear();
    return HPE_OK;
}

int RequestHeadParser::FinishHead(llhttp_t *parser) noexcept {
    RequestHeadParser *self = static_cast<RequestHeadParser *>(parser->data);
    if (llhttp_get_method(parser) != HTTP_GET ||
        llhttp_get_http_major(parser) != 1 ||
        llhttp_get_http_minor(parser) != 1 ||
        self->host_count != 1 || self->malformed)
        return HPE_USER;
    return HPE_PAUSED;
}

void RequestHeadParser::ProcessHeader() {
    string value = Trim(header_value);
    if (!strcasecmp(header_name.c_str(), "Host")) {
        host_count++;
        if (value.empty())
            malformed = true;
    } else if (!strcasecmp(header_name.c_str(), "Accept-Encoding")) {
        if (value.find("gzip") != string::npos)
            request.accepts_gzip = true;
    } else if (!strcasecmp(header_name.c_str(), "Connection")) {
        if (HasToken(value, "close"))
            request.close = true;
    } else if (!strcasecmp(header_name.c_str(), "Content-Length")) {
        content_length_count++;
        if (content_length_count != 1)
            malformed = true;
        if (value != "0") {
            bool zero = !value.empty() &&
                    value.find_first_not_of('0') == string::npos;
            if (zero)
                malformed = true;
            else
                request.unsupported_body = true;
        }
    } else if (!strcasecmp(header_name.c_str(), "Transfer-Encoding")) {
        request.unsupported_body = true;
    }
}

void RequestHeadParser::CountBytes(const char *data, size_t length) {
    head_size += length;
    if (line_complete)
        return;

    const char *newline = static_cast<const char *>(memchr(data, '\n', length));
    if (newline) {
        line_size += newline - data + 1;
        line_complete = true;
    } else {
        line_size += length;
    }
}

RequestHeadParser::Result RequestHeadParser::Parse(const char *data,
                                                   size_t length,
                                                   size_t *consumed) {
    llhttp_errno_t result = llhttp_execute(&parser, data, length);
    const char *position = llhttp_get_error_pos(&parser);
    if (result == HPE_OK) {
        *consumed = length;
    } else if (position != NULL && position >= data &&
               position <= data + length) {
        *consumed = position - data;
    } else {
        *consumed = 0;
    }
    CountBytes(data, *consumed);
    if (line_complete && !route_checked) {
        route_present = jpip::HasRoutingParameter(request.target);
        route_checked = true;
    }

    if ((!line_complete && line_size >= MAX_INITIAL_REQUEST_LINE) ||
        line_size > MAX_INITIAL_REQUEST_LINE || head_size > MAX_REQUEST_HEAD)
        return TOO_LARGE;
    if (result == HPE_PAUSED)
        return COMPLETE;
    if (result == HPE_OK)
        return INCOMPLETE;
    return MALFORMED;
}

bool RequestHeadParser::HasCompleteJPIPRequestLine() {
    return line_complete && llhttp_get_method(&parser) == HTTP_GET &&
           llhttp_get_http_major(&parser) == 1 &&
           llhttp_get_http_minor(&parser) == 1 && route_present;
}

bool RequestHeadParser::HasJPIPRoute() const {
    return route_checked ? route_present
                         : jpip::HasRoutingParameter(request.target);
}

const string &RequestHeadParser::GetTarget() const {
    return request.target;
}

RequestHead RequestHeadParser::TakeRequest() {
    RequestHead result = std::move(request);
    Reset();
    return result;
}

}
