#include "response_request.h"
#include <algorithm>
#include <climits>
using namespace std;
namespace jpip {
    void ResponseRequest::AddStream(uint64_t first, uint64_t last, uint64_t step) {
        codestream_selections.emplace_back(first, last, step);
        if (!has_stream) first_requested_codestream = first;
        has_stream = true;
    }

    void ResponseRequest::AddContext(int first, int last) {
        codestream_selections.emplace_back(first, last, 1);
    }

    bool ResponseRequest::SelectCodestreams(size_t available,
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
            if (selected->size() == ResponseRequest::MAX_CODESTREAM_INDEX + 1)
                return false;
            selected->push_back(static_cast<int>(id));
            return true;
        };

        for (const CodestreamSelection &selection : codestream_selections) {
            if (selection.step == 0 || selection.last < selection.first) return false;
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

    bool ResponseRequest::GetUnqualifiedModelCodestream(size_t available,
                                                int *codestream) const {
        uint64_t selected = has_stream ? first_requested_codestream : 0;
        if (selected >= available || selected > INT_MAX)
            return false;
        *codestream = static_cast<int>(selected);
        return true;
    }

}
