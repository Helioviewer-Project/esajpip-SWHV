#ifndef _HTTP_HEADER_H_
#define _HTTP_HEADER_H_

#include <iosfwd>
#include <string>

namespace http {

    class Header {
    public:
        std::string name;
        std::string value;

        Header() = default;
        Header(const std::string &_name, const std::string &_value);

        bool Parse(const std::string &line);
        bool Is(const char *header_name) const;

        friend std::ostream &operator<<(std::ostream &out, const Header &header);
        friend std::istream &operator>>(std::istream &in, Header &header);
    };
}

#endif /* _HTTP_HEADER_H_ */
