#include "trace.h"
#include "request.h"
#include "query.h"

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

        int Scale(int value, int numerator, int denominator) {
            uint64_t scaled = static_cast<uint64_t>(value) * numerator;
            return static_cast<int>((scaled + denominator - 1) / denominator);
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

        bool AppendRange(int first, int last, vector<int> *values) {
            size_t count = static_cast<size_t>(first > last ? first - last : last - first) + 1;
            if (values->size() > MAXC + 1 - count)
                return false;
            values->reserve(values->size() + count);
            if (first > last) {
                for (int i = first; i >= last; --i)
                    values->push_back(i);
            } else {
                for (int i = first; i <= last; ++i)
                    values->push_back(i);
            }
            return true;
        }

        bool ParseContext(const string &value, int *first, int *last) {
            string decoded;
            if (!Decode(value, &decoded) || decoded.compare(0, 5, "jpxl<") != 0 ||
                decoded.size() < 7 || decoded.back() != '>')
                return false;

            if (!ParseRange(decoded.substr(5, decoded.size() - 6), '-', first, last))
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
                        if (!ParseInteger(&position, &maximum_codestream))
                            return false;
                        maximum_codestream = Clamp(maximum_codestream,
                                                   minimum_codestream, MAXC);
                    }
                    if (*position++ != ']')
                        return false;
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
            woi->position.x = Scale(woi->position.x, res_image_size.x,
                                    resolution_size.x);
            woi->position.y = Scale(woi->position.y, res_image_size.y,
                                    resolution_size.y);
            woi->size.x = Scale(woi->size.x, res_image_size.x, resolution_size.x);
            woi->size.y = Scale(woi->size.y, res_image_size.y, resolution_size.y);
        }
        return res_image_size;
    }

    bool Request::Parse(const string &line) {
        string method, uri, protocol;

        if (line.empty())
            return false;

        istringstream in(line);
        if (!(in >> method >> uri >> protocol) || method != "GET" ||
            protocol != "HTTP/1.1")
            return false;

        return ParseURI(uri.substr(0, MAX_URI_LENGTH));
    }

    bool Request::ParseURI(const string &uri) {
        size_t question = uri.find('?');
        object = uri.substr(0, question);

        if (question == string::npos)
            return true;

        bool valid = true;
        Query query = ParseQuery(uri.data() + question + 1, uri.data() + uri.size());
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
                has.cnew = true;
            } else if (name == "cclose") {
                has.cclose = true;
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
                    else if (round.empty() || round == "closest")
                        round_direction = CLOSEST;
                    else
                        valid = false;
                    TRACE("JPIP parameter: fsiz=" << x << "," << y << "," << round);
                } else
                    valid = false;
            } else if (name == "roff") {
                if (ParsePair(value, &x, &y)) {
                    woi_position = jpeg2000::Point(x, y);
                    has.roff = true;
                    TRACE("JPIP parameter: roff=" << x << "," << y);
                } else
                    valid = false;
            } else if (name == "rsiz") {
                if (ParsePair(value, &x, &y)) {
                    woi_size = jpeg2000::Size(x, y);
                    has.rsiz = true;
                    TRACE("JPIP parameter: rsiz=" << x << "," << y);
                } else
                    valid = false;
            } else if (name == "len") {
                const char *position = value.c_str();
                if (ParseInteger(&position, &x) && *position == '\0' && x >= 0) {
                    length_response = x;
                    has.len = true;
                    TRACE("JPIP parameter: len=" << x);
                } else {
                    valid = false;
                }
            } else if (name == "stream") {
                if (ParseRange(value, ':', &x, &y)) {
                    x = Clamp(x, 0, MAXC);
                    y = Clamp(y, 0, MAXC);
                    if (AppendRange(x, y, &codestreams)) {
                        has.stream = true;
                        TRACE("JPIP parameter: stream=" << x << ":" << y);
                    } else {
                        valid = false;
                    }
                } else
                    valid = false;
            } else if (name == "model") {
                if (ParseModel(value, &model))
                    has.model = true;
                else
                    valid = false;
            } else if (name == "context") {
                if (ParseContext(value, &x, &y)) {
                    if (AppendRange(x, y, &codestreams)) {
                        has.context = true;
                        TRACE("JPIP parameter: context=" << value);
                    } else {
                        valid = false;
                    }
                } else
                    valid = false;
            }

            if (!value.empty())
                TRACE("JPIP parameter: " << name << "=" << value);
        }

        const string *route = FindParameter(query, has.cclose ? "cclose" : "cid");
        if (route)
            channel = *route;
        if ((has.roff || has.rsiz) && !has.fsiz)
            valid = false;
        return valid;
    }

}
