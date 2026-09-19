#include "query.h"

#include <cstring>

using namespace std;

namespace jpip {

    Query ParseQuery(const char *begin, const char *end) {
        Query query;

        while (begin < end) {
            const char *separator = static_cast<const char *>(memchr(begin, '&', end - begin));
            const char *parameter_end = separator ? separator : end;
            const char *equals = static_cast<const char *>(memchr(begin, '=', parameter_end - begin));
            const char *name_end = equals ? equals : parameter_end;

            query.push_back({string(begin, name_end),
                             string(equals ? equals + 1 : parameter_end, parameter_end)});

            if (!separator)
                break;
            begin = separator + 1;
        }
        return query;
    }

    Query ParseTargetQuery(const string &target) {
        size_t length = target.size() < MAX_URI_LENGTH
                ? target.size() : MAX_URI_LENGTH;
        size_t question = target.find('?');
        if (question == string::npos || question >= length)
            return Query();
        return ParseQuery(target.data() + question + 1,
                          target.data() + length);
    }

    const string *FindParameter(const Query &query, const char *name) {
        for (Query::const_reverse_iterator i = query.rbegin(); i != query.rend(); ++i)
            if (i->name == name)
                return &i->value;
        return NULL;
    }

    bool ParseUnsignedInteger(const char **position, uint64_t maximum, uint64_t *value) {
        const char *current = *position;
        if (*current < '0' || *current > '9')
            return false;

        uint64_t number = 0;
        do {
            uint64_t digit = *current - '0';
            if (digit > maximum || number > (maximum - digit) / 10)
                return false;
            number = number * 10 + digit;
            ++current;
        } while (*current >= '0' && *current <= '9');

        *position = current;
        *value = number;
        return true;
    }

}
