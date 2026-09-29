#include <climits>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "jpip/request/request.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static vector<int> SelectCodestreams(const jpip::Request &request,
                                     size_t available) {
    vector<int> selected;
    Check(request.SelectCodestreams(available, &selected),
          "Could not expand JPIP codestream selection");
    return selected;
}

static bool RejectRequest(const string &line) {
    jpip::Request request;
    return !request.ParseTarget(line);
}

static void CheckJHVRequests() {
    jpip::Request channel_request;
    Check(channel_request.ParseTarget(
              "/movie.jpx?cnew=http&type=jpp-stream&tid=0&len=512"),
          "Could not parse JHV channel request");
    Check(channel_request.object == "/movie.jpx", "Wrong channel target");
    Check(channel_request.routing.cnew, "Missing cnew field");
    Check(channel_request.routing.tid, "Missing target ID field");
    Check(channel_request.accepts_http, "JHV HTTP transport was not accepted");
    Check(channel_request.has.len && channel_request.length_response == 512,
          "Wrong channel response limit");

    jpip::Request absolute_request;
    Check(absolute_request.ParseTarget(
              "http://get.jpeg.org/images/kids.jp2?cnew=http") &&
              absolute_request.object == "/images/kids.jp2",
          "Could not extract the path from an absolute request target");

    jpip::Request mismatched_target_request;
    Check(mismatched_target_request.ParseTarget(
              "/jpip?cid=7&model=M0:100&tid=1") &&
              mismatched_target_request.routing.tid &&
              !mismatched_target_request.has.model &&
              mismatched_target_request.model.empty(),
          "Retained a cache model for a different target ID");

    jpip::Request matching_target_request;
    Check(matching_target_request.ParseTarget(
              "/jpip?cid=7&model=M0:100&tid=0") &&
              matching_target_request.has.model,
          "Discarded a cache model for the current target ID");

    jpip::Request handled_request;
    Check(handled_request.ParseTarget("/jpip?cid=7&handled") &&
              handled_request.routing.handled,
          "Missing handled field");

    jpip::Request transport_request;
    Check(transport_request.ParseTarget(
              "/movie.jpx?cnew=http-tcp,http") &&
              transport_request.accepts_http,
          "HTTP was not selected from a transport list");
    Check(transport_request.ParseTarget(
              "/movie.jpx?cnew=http-tcp") &&
              !transport_request.accepts_http,
          "An unsupported transport offer was not preserved");
    Check(RejectRequest("/movie.jpx?cnew=http,,http-tcp"),
          "Accepted a malformed transport list");

    jpip::Request target_request;
    Check(target_request.ParseTarget(
              "/jpip?target=movie.jpx&cnew=http&len=512"),
          "Could not parse target-form channel request");
    Check(target_request.routing.target && target_request.target == "movie.jpx",
          "Missing target field");

    jpip::Request quoted_target_request;
    Check(quoted_target_request.ParseTarget(
              "/jpip?target=a\"b.jp2&cnew=http") &&
              quoted_target_request.target == "a\"b.jp2",
          "Changed the request while preparing it for logging");

    jpip::Request metadata_request;
    Check(metadata_request.ParseTarget(
              "/jpip?stream=0&metareq=[*]!!&len=2000000&cid=7"),
          "Could not parse JHV metadata request");
    Check(metadata_request.routing.cid && metadata_request.channel == "7",
          "Missing channel ID");
    Check(metadata_request.target.empty(), "Unexpected target in metadata request");
    Check(metadata_request.has_metareq, "Missing metadata request");
    Check(SelectCodestreams(metadata_request, 1) == vector<int>(1, 0),
          "Wrong metadata codestream");
    Check(metadata_request.length_response == 2000000,
          "Wrong metadata response limit");

    jpip::Request frame_request;
    Check(frame_request.ParseTarget(
              "/jpip?stream=4013&fsiz=4096,4096,closest&rsiz=4096,4096&"
              "roff=0,0&len=2097152&cid=7"),
          "Could not parse JHV frame request");
    Check(frame_request.HasWOI(), "Missing frame window");
    Check(SelectCodestreams(frame_request, 4014) == vector<int>(1, 4013),
          "Wrong frame codestream");
    Check(frame_request.resolution_size == jpip::Size(4096, 4096),
          "Wrong frame resolution size");
    Check(frame_request.woi_size == jpip::Size(4096, 4096),
          "Wrong frame region size");
    Check(frame_request.woi_position == jpip::Point(0, 0),
          "Wrong frame region offset");
    Check(frame_request.round_direction == jpip::Request::CLOSEST,
          "Wrong frame rounding mode");
    Check(frame_request.length_response == 2097152, "Wrong frame response limit");

    jpip::Request default_round_request;
    Check(default_round_request.ParseTarget(
              "/jpip?fsiz=4096,4096&cid=7") &&
              default_round_request.round_direction == jpip::Request::ROUNDDOWN,
          "Frame size did not default to round-down");

    jpip::Request incomplete_window;
    Check(!incomplete_window.ParseTarget("/jpip?rsiz=1,1&cid=7") &&
              incomplete_window.resolution_size == jpip::Size(),
          "Accepted a region size without a frame size");

    Check(RejectRequest("/jpip?fsiz=abc&cid=7"),
          "Accepted a malformed frame size");
    Check(RejectRequest("/jpip?fsiz=1,1,sideways&cid=7"),
          "Accepted an unknown frame-size rounding mode");
    Check(RejectRequest("/jpip?roff=abc&cid=7"),
          "Accepted a malformed region offset");
    Check(RejectRequest("/jpip?rsiz=abc&cid=7"),
          "Accepted a malformed region size");
    Check(RejectRequest("/jpip?stream=abc&cid=7"),
          "Accepted a malformed codestream selector");
    Check(RejectRequest("/jpip?context=abc&cid=7"),
          "Accepted a malformed context selector");
    jpip::Request model_request;
    Check(model_request.ParseTarget("/jpip?stream=0&cid=7&model=M0"),
          "Could not parse terminal cache model");
    Check(model_request.has.model, "Missing terminal cache model");
    Check(model_request.model.size() == 1 &&
              model_request.model[0].bin_class == jpip::DataBinClass::META_DATA &&
              model_request.model[0].id == 0 &&
              model_request.model[0].amount == INT_MAX,
          "Wrong terminal metadata model");

    jpip::Request partial_model_request;
    Check(partial_model_request.ParseTarget(
              "/jpip?stream=0&cid=7&model=M0:446"),
          "Could not parse terminal partial cache model");
    Check(partial_model_request.has.model, "Missing terminal partial cache model");
    Check(partial_model_request.model.size() == 1 &&
              partial_model_request.model[0].amount == 446,
          "Wrong terminal partial metadata model");

    Check(RejectRequest("/jpip?model=M-1"),
          "Accepted negative metadata ID");
    Check(RejectRequest("/jpip?model=P-1"),
          "Accepted negative precinct ID");
    Check(RejectRequest("/jpip?model=M0:-1"),
          "Accepted negative model length");
    Check(RejectRequest("/jpip?model="), "Accepted empty cache model");
    Check(RejectRequest("/jpip?model=M0%"),
          "Accepted truncated cache-model escape");
    Check(RejectRequest("/jpip?model=M0%00M1"),
          "Accepted a cache model containing NUL");
    Check(RejectRequest("/jpip?len=-1&cid=7"),
          "Accepted negative response length");
    Check(RejectRequest("/jpip?len=+1&cid=7"),
          "Accepted a signed response length");
    Check(RejectRequest("/jpip?len=2147483648&cid=7"),
          "Accepted an overflowing response length");
    jpip::Request minimum_offset_request;
    Check(minimum_offset_request.ParseTarget(
              "/jpip?fsiz=1,1&roff=-2147483648,0&cid=7") &&
              minimum_offset_request.woi_position.x == INT_MIN,
          "Could not parse the minimum signed window offset");

    jpip::Request close_request;
    Check(close_request.ParseTarget("/jpip?cclose=7&len=0"),
          "Could not parse JHV close request");
    Check(close_request.routing.cclose && close_request.channel == "7", "Missing close field");
    Check(SelectCodestreams(close_request, 1).empty(),
          "Unexpected codestream in close request");

    jpip::Request new_channel_request;
    Check(new_channel_request.ParseTarget("/movie.jpx?cnew=http&len=512"),
          "Could not parse a new channel request");
    Check(new_channel_request.channel.empty(), "Unexpected channel ID in new channel request");

    jpip::Request duplicate_channel_request;
    Check(duplicate_channel_request.ParseTarget("/jpip?cid=46&cid=47") &&
              duplicate_channel_request.channel == "47",
          "Full parsing did not use the last channel ID");
    Check(RejectRequest("/jpip?cid=47&cclose=*"),
          "Accepted the unsupported all-channel close form");
    Check(RejectRequest("/image.jp2?cnew=http&cclose=47"),
          "Accepted conflicting channel creation and closure fields");
    jpip::Request close_priority_request;
    Check(close_priority_request.ParseTarget("/jpip?cclose=47&cid=48") &&
              close_priority_request.routing.cclose && close_priority_request.channel == "47",
          "Full parsing did not give cclose routing priority");

    jpip::Request selection_request;
    Check(selection_request.ParseTarget(
              "/jpip?stream=0,2-6:2,9-:3&context=jpxl%3C1-2%3E&cid=7"),
          "Could not parse codestream selectors");
    Check(SelectCodestreams(selection_request, 16) ==
                  vector<int>({0, 2, 4, 6, 9, 12, 15, 1}),
          "Wrong codestream selector ranges");
    jpip::Request overlapping_selection;
    Check(overlapping_selection.ParseTarget(
              "/jpip?stream=0-2,1-3&cid=7") &&
              SelectCodestreams(overlapping_selection, 4) ==
                      vector<int>({0, 1, 2, 3}),
          "Did not combine overlapping codestream ranges");
    jpip::Request ordered_selection;
    Check(ordered_selection.ParseTarget(
              "/jpip?stream=5-7,0-6&cid=7") &&
              SelectCodestreams(ordered_selection, 8) ==
                      vector<int>({5, 6, 7, 0, 1, 2, 3, 4}),
          "Did not preserve codestream selection order");
    jpip::Request context_first;
    int model_stream = -1;
    Check(context_first.ParseTarget(
              "/jpip?context=jpxl%3C0%3E&stream=1&stream=0&cid=7&model=P0") &&
              SelectCodestreams(context_first, 2) == vector<int>({0, 1}) &&
              context_first.GetUnqualifiedModelCodestream(2, &model_stream) &&
              model_stream == 1,
          "Context or a later stream changed the unqualified cache-model default");

    jpip::Request sampled_single_request;
    Check(sampled_single_request.ParseTarget(
              "/jpip?stream=0:1&cid=7") &&
              SelectCodestreams(sampled_single_request, 10) == vector<int>(1, 0),
          "Treated a sampling factor as a range endpoint");
    jpip::Request finite_range_request;
    Check(finite_range_request.ParseTarget(
              "/jpip?stream=0-1&cid=7") &&
              SelectCodestreams(finite_range_request, 10) ==
                      vector<int>({0, 1}),
          "Could not parse a finite codestream range");
    Check(RejectRequest("/jpip?stream=3-1&cid=7"),
          "Accepted a descending standard codestream range");
    Check(RejectRequest(
              "/jpip?context=jpxl%3C3-1%3E&cid=7"),
          "Accepted a descending JPX layer range");
    Check(RejectRequest("/jpip?stream=0-10:0&cid=7"),
          "Accepted a zero codestream sampling factor");
    Check(RejectRequest("/jpip?stream=0,,1&cid=7"),
          "Accepted an empty codestream range");
    jpip::Request oversized_selection;
    Check(oversized_selection.ParseTarget(
              "/jpip?stream=0-&cid=7"),
          "Could not parse an open codestream range");
    vector<int> oversized_codestreams;
    Check(!oversized_selection.SelectCodestreams(100002,
                                                  &oversized_codestreams),
          "Accepted an oversized expanded codestream selection");

    jpip::Request encoded_model_request;
    Check(encoded_model_request.ParseTarget(
              "/jpip?model=%5B1-2%5DHm:3,H4:5,P6:7,M8:9&cid=7"),
          "Could not parse encoded cache-model descriptors");
    Check(encoded_model_request.model.size() == 4 &&
              encoded_model_request.model[0].bin_class ==
                      jpip::DataBinClass::MAIN_HEADER &&
              encoded_model_request.model[0].first_codestream == 1 &&
              encoded_model_request.model[0].last_codestream == 2 &&
              encoded_model_request.model[0].amount == 3 &&
              encoded_model_request.model[1].bin_class ==
                      jpip::DataBinClass::TILE_HEADER &&
              encoded_model_request.model[1].id == 4 &&
              encoded_model_request.model[1].amount == 5 &&
              encoded_model_request.model[2].bin_class == jpip::DataBinClass::PRECINCT &&
              encoded_model_request.model[2].id == 6 &&
              encoded_model_request.model[2].amount == 7 &&
              encoded_model_request.model[3].bin_class == jpip::DataBinClass::META_DATA &&
              encoded_model_request.model[3].id == 8 &&
              encoded_model_request.model[3].amount == 9,
          "Wrong encoded cache model");

    jpip::Request open_model_request;
    Check(open_model_request.ParseTarget(
              "/jpip?model=%5B5-%5DHm&cid=7") &&
              open_model_request.model.size() == 1 &&
              open_model_request.model[0].first_codestream == 5 &&
              open_model_request.model[0].last_codestream == INT_MAX,
          "Could not parse an open cache-model codestream range");

    jpip::Request encoded_target_request;
    Check(encoded_target_request.ParseTarget(
              "/jpip?target=movie%20name.jpx&cnew=http") &&
              encoded_target_request.target == "movie%20name.jpx",
          "Unexpectedly decoded a target value");
    Check(RejectRequest("/jpip?len=12junk&cid=7"),
          "Accepted a response length with trailing data");
}

static void CheckRouteClassification() {
    const char *not_routes[] = {
        "/status?notcnew=http",
        "/status?cnewer=http",
        "/status?value=cnew",
        "/status?x=cid",
        "/status?close=cclose"
    };
    for (const char *target : not_routes)
        Check(!jpip::HasRoutingParameter(target),
              "A non-routing query field was classified as a JPIP route");
    Check(jpip::HasRoutingParameter("/image.jp2?cnew") &&
              jpip::HasRoutingParameter("/image.jp2?x=1&cid=7") &&
              jpip::HasRoutingParameter("/image.jp2?cclose=7&x=1"),
          "A JPIP routing field was not recognized");

}

static void CheckDiagnostics() {
    struct Case { const char *model; const char *error; };
    const Case cases[] = {
        {"-M0", "Subtractive bin-descriptors are not supported for model updating"},
        {"P0:L1", "Number of layers can not be used for model updating"},
        {"X0", "The bin-descriptor 'X' is not supported for model updating"}
    };
    for (const Case &test : cases) {
        jpip::Request request;
        string error;
        Check(!request.ParseTarget(string("/jpip?cid=7&model=") + test.model, &error),
              "Accepted an unsupported cache-model descriptor");
        Check(error == test.error, "Lost the cache-model diagnostic");
    }
}

int main() {
    CheckJHVRequests();
    CheckRouteClassification();
    CheckDiagnostics();
    return EXIT_SUCCESS;
}
