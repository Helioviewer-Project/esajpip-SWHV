#include "trace.h"
#include "request.h"
#include "query.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <climits>
#include <sstream>

#define MAXC 100000

using namespace std;

namespace jpip {

    namespace {

        int Clamp(int value, int minimum, int maximum) {
            return value < minimum ? minimum : (value > maximum ? maximum : value);
        }

        bool IsScheme(const string &uri, size_t length) {
            if (length == 0 ||
                !((uri[0] >= 'A' && uri[0] <= 'Z') ||
                  (uri[0] >= 'a' && uri[0] <= 'z')))
                return false;
            for (size_t i = 1; i < length; ++i) {
                char c = uri[i];
                if (!((c >= 'A' && c <= 'Z') ||
                      (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '+' || c == '-' ||
                      c == '.'))
                    return false;
            }
            return true;
        }

        string GetObject(const string &uri, size_t end) {
            if (uri.empty() || uri[0] == '/')
                return uri.substr(0, end);

            size_t colon = uri.find(':');
            if (colon == string::npos || colon >= end || !IsScheme(uri, colon))
                return uri.substr(0, end);

            size_t path = colon + 1;
            if (path + 1 < end && uri[path] == '/' && uri[path + 1] == '/') {
                path = uri.find('/', path + 2);
                if (path == string::npos || path >= end)
                    return "/";
            }
            return uri.substr(path, end - path);
        }

        void MapInterval(int selected_size, int requested_size,
                         int *offset, int *length) {
            assert(selected_size > 0 && requested_size > 0);
            assert(*offset >= 0 && *length >= 0 &&
                   *offset <= requested_size &&
                   *length <= requested_size - *offset);

            uint64_t end = static_cast<uint64_t>(*offset) + *length;
            int mapped_offset = static_cast<int>(
                    static_cast<uint64_t>(*offset) * selected_size /
                    requested_size);
            uint64_t mapped_end = end * selected_size;
            mapped_end = (mapped_end + requested_size - 1) / requested_size;
            *offset = mapped_offset;
            *length = static_cast<int>(mapped_end) - mapped_offset;
        }

        void SetError(string *error_message, const char *message) {
            if (error_message != NULL && error_message->empty())
                *error_message = message;
        }

        bool ParseInteger(const char **position, int *value) {
            const char *current = *position;
            bool negative = *current == '-';
            if (negative)
                ++current;

            uint64_t maximum = negative ? static_cast<uint64_t>(INT_MAX) + 1 : INT_MAX;
            uint64_t number;
            if (!ParseUnsignedInteger(&current, maximum, &number))
                return false;
            *position = current;
            if (negative)
                *value = number == maximum ? INT_MIN : -static_cast<int>(number);
            else
                *value = static_cast<int>(number);
            return true;
        }

        bool ParsePair(const string &text, int *x, int *y, string *suffix = NULL) {
            const char *position = text.c_str();
            if (!ParseInteger(&position, x) || *position++ != ',' ||
                !ParseInteger(&position, y))
                return false;
            if (*position == '\0') {
                if (suffix)
                    suffix->clear();
                return true;
            }
            if (!suffix || *position++ != ',')
                return false;
            *suffix = position;
            return !suffix->empty();
        }

        bool ParseRange(const string &text, char separator, int *first, int *last) {
            const char *position = text.c_str();
            if (!ParseInteger(&position, first))
                return false;
            *last = *first;
            if (*position != '\0') {
                if (*position++ != separator || !ParseInteger(&position, last))
                    return false;
            }
            return *position == '\0';
        }

        bool ParseTransports(const string &value, bool *accepts_http) {
            *accepts_http = false;
            size_t begin = 0;
            for (;;) {
                size_t end = value.find(',', begin);
                if (end == string::npos)
                    end = value.size();
                if (end == begin)
                    return false;
                for (size_t i = begin; i < end; ++i) {
                    char c = value[i];
                    if (!((c >= 'A' && c <= 'Z') ||
                          (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') ||
                          c == '.' || c == '_' || c == '-'))
                        return false;
                }
                if (value.compare(begin, end - begin, "http") == 0)
                    *accepts_http = true;
                if (end == value.size())
                    return true;
                begin = end + 1;
            }
        }

        bool HexDigit(char character, unsigned char *value) {
            if (character >= '0' && character <= '9')
                *value = character - '0';
            else if (character >= 'a' && character <= 'f')
                *value = character - 'a' + 10;
            else if (character >= 'A' && character <= 'F')
                *value = character - 'A' + 10;
            else
                return false;
            return true;
        }

        bool Decode(const string &text, string *decoded) {
            decoded->clear();
            decoded->reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] != '%') {
                    decoded->push_back(text[i]);
                    continue;
                }
                unsigned char high, low;
                if (i + 2 >= text.size() ||
                    !HexDigit(text[i + 1], &high) || !HexDigit(text[i + 2], &low))
                    return false;
                char character = static_cast<char>((high << 4) | low);
                if (character == '\0')
                    return false;
                decoded->push_back(character);
                i += 2;
            }
            return true;
        }

        bool ParseContext(const string &value, int *first, int *last) {
            string decoded;
            if (!Decode(value, &decoded) || decoded.compare(0, 5, "jpxl<") != 0 ||
                decoded.size() < 7 || decoded.back() != '>')
                return false;

            if (!ParseRange(decoded.substr(5, decoded.size() - 6), '-', first, last) ||
                *first < 0 || *last < *first)
                return false;
            *first = Clamp(*first, 0, MAXC);
            *last = Clamp(*last, 0, MAXC);
            return true;
        }

        bool ParseModel(const string &value, vector<Request::ModelUpdate> *model) {
            string text;
            if (!Decode(value, &text) || text.empty())
                return false;

            model->clear();
            int minimum_codestream = 0;
            int maximum_codestream = 0;
            bool has_codestream_qualifier = false;
            bool descriptor_found = false;
            const char *position = text.c_str();

            while (*position != '\0') {
                if (*position == ',') {
                    if (*++position == '\0')
                        return false;
                    continue;
                }
                if (*position == '[') {
                    ++position;
                    if (!ParseInteger(&position, &minimum_codestream))
                        return false;
                    minimum_codestream = Clamp(minimum_codestream, 0, MAXC);
                    maximum_codestream = minimum_codestream;
                    if (*position == '-') {
                        ++position;
                        if (*position == ']')
                            maximum_codestream = INT_MAX;
                        else {
                            if (!ParseInteger(&position, &maximum_codestream))
                                return false;
                            maximum_codestream = Clamp(maximum_codestream,
                                                       minimum_codestream, MAXC);
                        }
                    }
                    if (*position++ != ']')
                        return false;
                    has_codestream_qualifier = true;
                    continue;
                }
                if (*position == '-') {
                    ERROR("Subtractive bin-descriptors are not supported for model updating");
                    return false;
                }

                char type = *position++;
                int id = 0;
                if (type == 'H' && *position == 'm') {
                    type = 'h';
                    ++position;
                } else if (!ParseInteger(&position, &id) || id < 0) {
                    return false;
                }

                int amount = INT_MAX;
                if (*position == ':') {
                    ++position;
                    if (*position == 'L') {
                        ERROR("Number of layers can not be used for model updating");
                        return false;
                    }
                    if (!ParseInteger(&position, &amount) || amount < 0)
                        return false;
                }

                Request::ModelUpdate update;
                update.has_codestream_qualifier = has_codestream_qualifier;
                update.first_codestream = minimum_codestream;
                update.last_codestream = maximum_codestream;
                update.id = id;
                update.amount = amount;
                if (type == 'M') {
                    update.bin_class = DataBinClass::META_DATA;
                    TRACE("Model updating: M" << id << ":" << (amount == INT_MAX ? -1 : amount));
                } else if (type == 'h') {
                    update.bin_class = DataBinClass::MAIN_HEADER;
                    TRACE("Model updating: Hm:" << (amount == INT_MAX ? -1 : amount));
                } else if (type == 'H') {
                    update.bin_class = DataBinClass::TILE_HEADER;
                    TRACE("Model updating: H" << id << ":" << (amount == INT_MAX ? -1 : amount));
                } else if (type == 'P') {
                    update.bin_class = DataBinClass::PRECINCT;
                    TRACE("Model updating: P" << id << ":" << (amount == INT_MAX ? -1 : amount));
                } else {
                    ERROR("The bin-descriptor '" << type << "' is not supported for model updating");
                    return false;
                }
                model->push_back(update);
                descriptor_found = true;
            }
            return descriptor_found;
        }

    }

    bool Request::ParseStream(const string &value) {
        const char *position = value.c_str();
        uint64_t first_requested = 0;
        bool first = true;
        while (*position != '\0') {
            uint64_t range_first;
            if (!ParseUnsignedInteger(&position, UINT64_MAX, &range_first))
                return false;
            if (first) {
                first_requested = range_first;
                first = false;
            }

            uint64_t last = range_first;
            if (*position == '-') {
                ++position;
                if (*position == '\0' || *position == ',' || *position == ':')
                    last = UINT64_MAX;
                else if (!ParseUnsignedInteger(&position, UINT64_MAX, &last) ||
                         last < range_first)
                    return false;
            }

            uint64_t step = 1;
            if (*position == ':') {
                ++position;
                if (!ParseUnsignedInteger(&position, UINT64_MAX, &step) || step == 0)
                    return false;
            }

            codestream_selections.emplace_back(range_first, last, step);
            if (*position == '\0') {
                if (!has.stream)
                    first_requested_codestream = first_requested;
                return true;
            }
            if (*position++ != ',' || *position == '\0')
                return false;
        }
        return false;
    }

    void Request::AddContext(int first, int last) {
        codestream_selections.emplace_back(first, last, 1);
    }

    bool Request::SelectCodestreams(size_t available,
                                    vector<int> *selected) const {
        selected->clear();
        uint64_t limit = min<uint64_t>(available,
                                       static_cast<uint64_t>(INT_MAX) + 1);
        vector<bool> seen(limit);
        auto append = [&](uint64_t id) {
            if (id >= limit)
                return true;
            if (seen[id])
                return true;
            seen[id] = true;
            if (selected->size() == MAXC + 1)
                return false;
            selected->push_back(static_cast<int>(id));
            return true;
        };

        for (const CodestreamSelection &selection : codestream_selections) {
            if (selection.first >= limit)
                continue;
            uint64_t last = selection.last == UINT64_MAX
                            ? limit - 1
                            : min(selection.last, limit - 1);
            for (uint64_t id = selection.first; id <= last;) {
                if (!append(id))
                    return false;
                if (selection.step > last - id)
                    break;
                id += selection.step;
            }
        }
        return true;
    }

    bool Request::GetUnqualifiedModelCodestream(size_t available,
                                                int *codestream) const {
        uint64_t selected = has.stream ? first_requested_codestream : 0;
        if (selected >= available || selected > INT_MAX)
            return false;
        *codestream = static_cast<int>(selected);
        return true;
    }

    jpeg2000::Size Request::GetResolution(
            const jpeg2000::CodingParameters *coding_parameters, WOI *woi) const {
        jpeg2000::Size res_image_size;

        if (round_direction == CLOSEST)
            woi->resolution = coding_parameters->GetClosestResolution(resolution_size,
                                                                       &res_image_size);
        else if (round_direction == ROUNDUP)
            woi->resolution = coding_parameters->GetRoundUpResolution(resolution_size,
                                                                       &res_image_size);
        else
            woi->resolution = coding_parameters->GetRoundDownResolution(resolution_size,
                                                                         &res_image_size);

        if (resolution_size.x > 0 && resolution_size.y > 0 &&
            resolution_size != res_image_size) {
            MapInterval(res_image_size.x, resolution_size.x,
                        &woi->position.x, &woi->size.x);
            MapInterval(res_image_size.y, resolution_size.y,
                        &woi->position.y, &woi->size.y);
        }
        return res_image_size;
    }

    bool Request::Parse(const string &line, string *error_message) {
        string method, uri, protocol, extra;

        if (error_message != NULL)
            error_message->clear();

        if (line.empty()) {
            SetError(error_message, "The HTTP request line is empty");
            return false;
        }

        istringstream in(line);
        if (!(in >> method >> uri >> protocol) || method != "GET" ||
            protocol != "HTTP/1.1" || (in >> extra)) {
            SetError(error_message, "The request must use HTTP/1.1 GET");
            return false;
        }

        return ParseTarget(uri, error_message);
    }

    bool Request::ParseTarget(const string &target, string *error_message) {
        if (error_message != NULL)
            error_message->clear();
        return ParseURI(target.substr(0, MAX_URI_LENGTH), error_message);
    }

    bool Request::ParseURI(const string &uri, string *error_message) {
        size_t question = uri.find('?');
        object = GetObject(uri, question);

        if (question == string::npos)
            return true;

        bool valid = true;
        Query query = ParseQuery(uri.data() + question + 1, uri.data() + uri.size());
        const string *tid = FindParameter(query, "tid");
        bool accept_model = tid == NULL || *tid == "0";
        for (const QueryParameter &parameter : query) {
            const string &name = parameter.name;
            const string &value = parameter.value;
            int x, y;

            if (name == "target") {
                has.target = true;
                target = value;
            } else if (name == "cid") {
                has.cid = true;
            } else if (name == "cnew") {
                if (ParseTransports(value, &accepts_http))
                    has.cnew = true;
                else {
                    valid = false;
                    SetError(error_message, "Invalid JPIP cnew parameter");
                }
            } else if (name == "cclose") {
                if (value == "*") {
                    valid = false;
                    SetError(error_message,
                             "Closing every JPIP channel is not supported");
                } else {
                    has.cclose = true;
                }
            } else if (name == "metareq") {
                has.metareq = true;
            } else if (name == "fsiz") {
                string round;
                if (ParsePair(value, &x, &y, &round)) {
                    resolution_size = jpeg2000::Size(x, y);
                    has.fsiz = true;
                    if (round == "round-up")
                        round_direction = ROUNDUP;
                    else if (round == "round-down")
                        round_direction = ROUNDDOWN;
                    else if (round == "closest")
                        round_direction = CLOSEST;
                    else if (round.empty())
                        round_direction = ROUNDDOWN;
                    else {
                        valid = false;
                        SetError(error_message, "Invalid JPIP fsiz parameter");
                    }
                    TRACE("JPIP parameter: fsiz=" << x << "," << y << "," << round);
                } else {
                    valid = false;
                    SetError(error_message, "Invalid JPIP fsiz parameter");
                }
            } else if (name == "roff") {
                if (ParsePair(value, &x, &y)) {
                    woi_position = jpeg2000::Point(x, y);
                    has.roff = true;
                    TRACE("JPIP parameter: roff=" << x << "," << y);
                } else {
                    valid = false;
                    SetError(error_message, "Invalid JPIP roff parameter");
                }
            } else if (name == "rsiz") {
                if (ParsePair(value, &x, &y)) {
                    woi_size = jpeg2000::Size(x, y);
                    has.rsiz = true;
                    TRACE("JPIP parameter: rsiz=" << x << "," << y);
                } else {
                    valid = false;
                    SetError(error_message, "Invalid JPIP rsiz parameter");
                }
            } else if (name == "len") {
                const char *position = value.c_str();
                if (ParseInteger(&position, &x) && *position == '\0' && x >= 0) {
                    length_response = x;
                    has.len = true;
                    TRACE("JPIP parameter: len=" << x);
                } else {
                    valid = false;
                    SetError(error_message, "Invalid JPIP len parameter");
                }
            } else if (name == "tid") {
                has.tid = true;
            } else if (name == "handled") {
                has.handled = true;
            } else if (name == "stream") {
                if (ParseStream(value)) {
                    has.stream = true;
                    TRACE("JPIP parameter: stream=" << value);
                } else {
                    valid = false;
                    SetError(error_message, "Invalid JPIP stream parameter");
                }
            } else if (name == "model" && accept_model) {
                if (ParseModel(value, &model)) {
                    has.model = true;
                } else {
                    valid = false;
                    SetError(error_message,
                             "Invalid or unsupported JPIP model parameter");
                }
            } else if (name == "context") {
                if (ParseContext(value, &x, &y)) {
                    AddContext(x, y);
                    has.context = true;
                    TRACE("JPIP parameter: context=" << value);
                } else {
                    valid = false;
                    SetError(error_message, "Invalid JPIP context parameter");
                }
            }

            if (!value.empty())
                TRACE("JPIP parameter: " << name << "=" << value);
        }

        const string *route = FindParameter(query, has.cclose ? "cclose" : "cid");
        if (route)
            channel = *route;
        if (has.cnew && has.cclose) {
            valid = false;
            SetError(error_message,
                     "JPIP cnew and cclose can not be combined");
        }
        if ((has.roff || has.rsiz) && !has.fsiz) {
            valid = false;
            SetError(error_message,
                     "JPIP fsiz is required with roff or rsiz");
        }
        return valid;
    }

}
