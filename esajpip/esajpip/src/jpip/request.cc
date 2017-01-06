#include "trace.h"
#include "request.h"

namespace jpip
{

    void Request::ParseParameters(istream& stream) {
        mask.Clear();
        http::Request::ParseParameters(stream);
    }

    void Request::ParseParameter(istream& stream, const string& param, string& value) {
        char c;
        int x, y;
        string aux;

        if (param == "target") mask.items.target = 1;
        else if (param == "cid") mask.items.cid = 1;
        else if (param == "cnew") mask.items.cnew = 1;
        else if (param == "cclose") mask.items.cclose = 1;
        else if (param == "metareq") mask.items.metareq = 1;
        else if (param == "fsiz") {
            if ((stream >> x >> c >> y) && (c == ',')) {
                resolution_size.x = x;
                resolution_size.y = y;
                mask.items.fsiz = 1;

                if (stream.peek() == ',') {
                    getline(stream.ignore(1), aux, '&');
                    if (!stream.eof()) stream.unget();

                    if (aux == "round-up") round_direction = ROUNDUP;
                    else if (aux == "round-down") round_direction = ROUNDDOWN;
                    else round_direction = CLOSEST;
                }

                TRACE("JPIP parameter: fsiz=" << resolution_size.x << "," << resolution_size.y << "," << aux);
            }
        } else if (param == "roff") {
            if ((stream >> x >> c >> y) && (c == ',')) {
                woi_position.x = x;
                woi_position.y = y;
                mask.items.roff = 1;

                TRACE("JPIP parameter: roff=" << woi_position.x << "," << woi_position.y);
            }
        } else if (param == "rsiz") {
            if ((stream >> x >> c >> y) && (c == ',')) {
                woi_size.x = x;
                woi_size.y = y;
                mask.items.rsiz = 1;

                TRACE("JPIP parameter: rsiz=" << woi_size.x << "," << woi_size.y);
            }
        } else if (param == "len") {
            if (stream >> x) {
                length_response = x;
                mask.items.len = 1;

                TRACE("JPIP parameter: len=" << length_response);
            }
        } else if (param == "stream") {
            if (stream >> x) {
                y = x;

                if (stream.peek() == ':')
                    stream.ignore(1) >> y;

                if (stream) {
                    min_codestream = x;
                    max_codestream = y;
                    mask.items.stream = 1;
                }

                TRACE("JPIP parameter: stream=" << min_codestream << ":" << max_codestream);
            }
        } else if (param == "model") {
            if (ParseModel(stream))
                mask.items.model = 1;
        } else if (param == "context") {
            char jpxl_param[5];
            stream.get(jpxl_param, 5);

            if (!strcmp(jpxl_param, "jpxl")) {
                char c;
                GetCodedChar(stream, c);
                if (c == '<') {
                    if (stream >> x) {
                        y = x;

                        if (stream.peek() == '-')
                            stream.ignore(1) >> y;

                        GetCodedChar(stream, c);
                        if (c=='>') {
                            min_codestream = x;
                            max_codestream = y;
                            mask.items.context = 1;
                        }
                    }
                }
                TRACE("JPIP parameter: context=" << min_codestream << ":" << max_codestream);
            }
        }

        getline(stream, value, '&');

        if (value.size() > 0)
            TRACE("JPIP parameter: " << param << "=" << value);
    }

    istream& Request::GetCodedChar(istream& in, char& c) {
        if (in.get(c)) {
            if (c == '%') {
                int cval;
                stringstream hex_str;

                hex_str.put(in.get());
                hex_str.put(in.get());

                if (hex_str >> hex >> cval) c = (char)cval;
                else {
                    in.setstate(istream::failbit);
                    c = EOF;
                }
            }
        }
        return in;
    }

    istream& Request::ParseModel(istream& in) {
        char c;
        int id, amount;
        int minc = 0, maxc = 0;

        cache_model.Clear();

        while (in.good()) {
            GetCodedChar(in, c);

            if (c == ',') continue;
            else if (c == '&') {
                in.unget();
                break;
            } else if (c == '[') {
                if (in >> minc) {
                    maxc = minc;

                    if (in.peek() == '-')
                        in.ignore(1) >> maxc;
                    if (!GetCodedChar(in, c) || (c != ']'))
                        in.setstate(istream::failbit);

                    TRACE("Model updating: [" << minc << "-" << maxc << "]");
                }
            } else if (c == '-') {
                ERROR("Subtractive bin-descriptors are not supported for model updating");
                in.setstate(istream::failbit);
            } else {
                if ((c == 'H') && (in.peek() == 'm')) {
                    in.ignore(1);
                    c = 'h';
                } else {
                    in >> id;
                }

                amount = INT_MAX;
                if (in.peek() == ':') {
                    if (in.ignore(1).peek() != 'L') in >> amount;
                    else {
                        ERROR("Number of layers can not be used for model updating");
                        in.setstate(istream::failbit);
                    }
                }

                if (in) {
                    if (c == 'M') {
                        cache_model.AddToMetadata(id, amount);
                        TRACE("Model updating: M" << id << ":" << (amount == INT_MAX ? -1 : amount));
                    } else {
                        for (int i = minc; i <= maxc; i++) {
                            CacheModel::Codestream& cod = cache_model.GetCodestream(i);

                            if (c == 'h') {
                                cod.AddToMainHeader(amount);
                                TRACE("Model updating: Hm" << ":" << (amount == INT_MAX ? -1 : amount));
                            } else if (c == 'H') {
                                cod.AddToTileHeader(amount);
                                TRACE("Model updating: H" << id << ":" << (amount == INT_MAX ? -1 : amount));
                            } else if (c == 'P') {
                                cod.AddToPrecinct(id, amount);
                                TRACE("Model updating: P" << id << ":" << (amount == INT_MAX ? -1 : amount));
                            } else {
                                ERROR("The bin-descriptor '" << c << "' is not supported for model updating");
                                in.setstate(istream::failbit);
                                break;
                            }
                        }
                    }
                }
            }
        }

        return in;
    }
}
