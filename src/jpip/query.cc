#include "query.h"

#include <cstring>

using namespace std;

namespace jpip {
namespace {

    bool IsRoutingParameter(const char *begin, const char *end) {
        size_t length = end - begin;
        return (length == 4 && memcmp(begin, "cnew", 4) == 0) ||
               (length == 3 && memcmp(begin, "cid", 3) == 0) ||
               (length == 6 && memcmp(begin, "cclose", 6) == 0);
    }

    template<typename Function>
    bool ForEachParameter(const char *begin, const char *end,
                          Function function) {
        while (begin < end) {
            const char *separator = static_cast<const char *>(
                    memchr(begin, '&', end - begin));
            const char *parameter_end = separator ? separator : end;
            const char *equals = static_cast<const char *>(
                    memchr(begin, '=', parameter_end - begin));
            const char *name_end = equals ? equals : parameter_end;
            const char *value_begin = equals ? equals + 1 : parameter_end;
            if (!function(begin, name_end, value_begin, parameter_end))
                return false;
            if (!separator)
                break;
            begin = separator + 1;
        }
        return true;
    }

    Query ParseQuery(const char *begin, const char *end) {
        Query query;
        ForEachParameter(begin, end,
                         [&query](const char *name_begin,
                                  const char *name_end,
                                  const char *value_begin,
                                  const char *value_end) {
                             query.push_back({string(name_begin, name_end),
                                              string(value_begin, value_end)});
                             return true;
                         });
        return query;
    }

}

    Query ParseTargetQuery(const string &target) {
        size_t question = target.find('?');
        if (question == string::npos)
            return Query();
        return ParseQuery(target.data() + question + 1,
                          target.data() + target.size());
    }

    bool HasRoutingParameter(const string &target) {
        size_t question = target.find('?');
        if (question == string::npos)
            return false;

        const char *begin = target.data() + question + 1;
        const char *end = target.data() + target.size();
        bool visited_all = ForEachParameter(
                begin, end,
                [](const char *name_begin, const char *name_end,
                   const char *, const char *) {
                    return !IsRoutingParameter(name_begin, name_end);
                });
        return !visited_all;
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
