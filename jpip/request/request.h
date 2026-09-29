#pragma once

#include <string>
#include "response_request.h"

namespace jpip {
// Classifies routing fields without constructing a parsed request.
bool HasRoutingParameter(const std::string &target);

// Parses the supported JPIP request syntax without opening targets or channels.
// ResponseRequest contains the fields consumed by response generation;
// routing fields are interpreted by the caller managing the channels.
class Request : public ResponseRequest {
    bool ParseStream(const std::string &value);
public:
    struct Routing {
        bool target = false, tid = false, handled = false;
        bool cid = false, cnew = false, cclose = false;
    } routing;
    std::string object = "/", target, channel;
    bool accepts_http = false;
    bool has_metareq = false; // Server gzip policy; metadata selection is fixed.
    bool ParseTarget(const std::string &uri, std::string *error_message = NULL);
};
}
