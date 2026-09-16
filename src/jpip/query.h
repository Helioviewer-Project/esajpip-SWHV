#ifndef _JPIP_QUERY_H_
#define _JPIP_QUERY_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jpip {

    static const std::size_t MAX_URI_LENGTH = 1023;

    struct QueryParameter {
        std::string name;
        std::string value;
    };

    using Query = std::vector<QueryParameter>;

    Query ParseQuery(const char *begin, const char *end);
    const std::string *FindParameter(const Query &query, const char *name);
    bool ParseUnsignedInteger(const char **position, uint64_t maximum, uint64_t *value);

}

#endif /* _JPIP_QUERY_H_ */
