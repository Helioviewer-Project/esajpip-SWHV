#include "jpip_support.h"
#include <climits>
#include <string>
#include "jpip/request/request.h"

namespace {
bool routing(const std::string &uri) {
    size_t at = uri.find('?');
    if (at == std::string::npos) return false;
    do {
        ++at;
        size_t end = uri.find('&', at);
        if (end == std::string::npos) end = uri.size();
        size_t equals = uri.find('=', at);
        std::string name = uri.substr(at, std::min(equals, end) - at);
        if (name == "cid" || name == "cnew" || name == "cclose") return true;
        at = end;
    } while (at < uri.size());
    return false;
}
}

extern "C" int replay_jpip_request(const uint8_t *data, size_t size) {
    using namespace jpip_fuzz;
    std::string raw(size ? reinterpret_cast<const char *>(data) : "", std::min(size, size_t(4096)));
    require(jpip::HasRoutingParameter(raw) == routing(raw), "routing classifier disagrees with field names");
    jpip::Request request;
    std::string error;
    if (request.ParseTarget(raw, &error)) {
        std::vector<int> selected;
        require(request.SelectCodestreams(16, &selected), "parsed ranges cannot be expanded");
        std::vector<bool> seen(16);
        for (int id : selected) {
            require(id >= 0 && id < 16 && !seen[id], "selected codestream outside target or repeated");
            seen[id] = true;
        }
    }

    // Construct valid overlapping selectors and enumerate their union without
    // using the production range expansion. Context must not change the model default.
    Input input(data, size);
    size_t available = input.byte() % 17;
    std::string query = "/jpip?";
    std::vector<int> expected;
    int first_stream = -1;
    for (int i = 0; i < 4; ++i) {
        bool context = input.byte() & 1;
        uint64_t first = input.boundary(), last = input.boundary();
        if (last < first) std::swap(first, last);
        bool open = input.byte() & 1;
        uint64_t step = context ? 1 : std::max(uint64_t(1), input.boundary());
        std::string range = std::to_string(first) + "-" + (open ? "" : std::to_string(last));
        query += context ? "context=jpxl<" + range + ">" : "stream=" + range + ":" + std::to_string(step);
        query += '&';
        if (!context && first_stream == -1)
            first_stream = first <= INT_MAX ? static_cast<int>(first) : INT_MAX;
        for (uint64_t id = 0; id < available; ++id)
            if (id >= first && (open || id <= last) && (id - first) % step == 0 &&
                std::find(expected.begin(), expected.end(), static_cast<int>(id)) == expected.end())
                expected.push_back(static_cast<int>(id));
    }
    query += "model=P0&cid=7";
    jpip::Request selectors;
    require(selectors.ParseTarget(query, &error), "constructed selectors rejected");
    std::vector<int> actual;
    require(selectors.SelectCodestreams(available, &actual) && actual == expected,
            "selector order, bounds, sampling or deduplication differs from reference");
    int model_stream = -1;
    int default_stream = first_stream == -1 ? 0 : first_stream;
    bool has_default = selectors.GetUnqualifiedModelCodestream(available, &model_stream);
    require(has_default == (static_cast<size_t>(default_stream) < available) &&
            (!has_default || model_stream == default_stream), "wrong unqualified model codestream");

    const unsigned limits[] = {0, 1, 3, 9, 99999, 100000};
    unsigned first = limits[input.byte() % 6], last = limits[input.byte() % 6];
    if (last < first) std::swap(first, last);
    unsigned mode = input.byte() % 7;
    std::string range;
    if (mode == 0) range = std::to_string(first);
    if (mode == 1) range = std::to_string(first) + "-" + std::to_string(last);
    if (mode == 2) range = std::to_string(first) + "-";
    if (mode == 3) range = "-" + std::to_string(first) + "-" + std::to_string(last);
    if (mode == 4) range = std::to_string(first + 1) + "-" + std::to_string(first);
    if (mode == 5) range = "0-100001";
    if (mode == 6) range = "0-18446744073709551615";
    jpip::Request model;
    bool accepted = model.ParseTarget("/jpip?model=[" + range + "]P0:1&cid=8", &error);
    require(accepted == (mode < 3), "model range acceptance disagrees with unsigned range grammar");
    require(model.channel == "8" && model.routing.cid, "model error lost following route");
    if (accepted) {
        require(model.has.model && model.model.size() == 1 &&
                model.model[0].first_codestream == static_cast<int>(first) &&
                model.model[0].last_codestream == (mode == 2 ? INT_MAX : static_cast<int>(mode == 0 ? first : last)),
                "model range was clamped or changed");
    } else {
        require(!error.empty() && !model.has.model && model.model.empty(), "invalid qualifier produced an update");
    }
    const int dimensions[] = {INT_MIN, -1, 0, 1, 1024, INT_MAX};
    int x = dimensions[input.byte() % 6], y = dimensions[input.byte() % 6];
    const char *rounds[] = {"", ",round-down", ",round-up", ",closest", ",sideways", ",round-up,extra"};
    unsigned round = input.byte() % 6;
    jpip::Request window;
    require(window.ParseTarget("/jpip?fsiz=640,480,round-up"), "initial window rejected");
    accepted = window.ParseTarget("/jpip?fsiz=" + std::to_string(x) + "," +
                                  std::to_string(y) + rounds[round] + "&cid=9", &error);
    require(accepted == (round < 4) && window.has.fsiz && window.channel == "9",
            "frame-size syntax acceptance or following route changed");
    if (accepted) {
        jpip::ResponseRequest::RoundDirection direction = round < 2 ? jpip::ResponseRequest::ROUNDDOWN :
                round == 2 ? jpip::ResponseRequest::ROUNDUP : jpip::ResponseRequest::CLOSEST;
        require(window.resolution_size == jpip::Size(x, y) && window.round_direction == direction,
                "frame-size parser changed dimensions or rounding");
    } else {
        require(window.resolution_size == jpip::Size(640, 480) &&
                window.round_direction == jpip::ResponseRequest::ROUNDUP && !error.empty(),
                "invalid frame size changed previous window state");
    }
    return 0;
}

#ifndef ESAJPIP_FUZZ_REPLAY
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    return replay_jpip_request(data, size);
}
#endif
