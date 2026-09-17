#include "header.h"

#include <cstring>
#include <ostream>

#include "protocol.h"

namespace http {

    Header::Header(const std::string &_name, const std::string &_value)
        : name(_name), value(_value) {
    }

    bool Header::Parse(const std::string &line) {
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            return false;

        size_t value_begin = colon + 1;
        while (value_begin < line.size() &&
               (line[value_begin] == ' ' || line[value_begin] == '\t'))
            value_begin++;

        size_t value_end = line.size();
        while (value_end > value_begin &&
               (line[value_end - 1] == ' ' || line[value_end - 1] == '\t'))
            value_end--;

        name = line.substr(0, colon);
        value = line.substr(value_begin, value_end - value_begin);
        return true;
    }

    bool Header::Is(const char *header_name) const {
        return strcasecmp(name.c_str(), header_name) == 0;
    }

    std::ostream &operator<<(std::ostream &out, const Header &header) {
        return out << header.name << ": " << header.value << CRLF;
    }

}
