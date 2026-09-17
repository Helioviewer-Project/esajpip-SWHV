#include <sys/socket.h>

#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "config.h"
#include "server/connection_queue.h"
#include "server/initial_request.h"
#include "data/file.h"
#include "data/file_segment.h"
#include "http/header.h"
#include "http/protocol.h"
#include "http/response.h"
#include "jpeg2000/place_holder.h"
#include "jpip/cache_model.h"
#include "jpip/databin_writer.h"
#include "jpip/jpip.h"
#include "jpip/request.h"
#include "jpip/woi_composer.h"
#include "net/address.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static bool LoadConfig(const char *contents, Config *config,
                       string *error_message = NULL) {
    char path[] = "/tmp/esajpip-config-XXXXXX";
    int fd = mkstemp(path);
    Check(fd >= 0, "Could not create configuration test file");
    size_t size = strlen(contents);
    Check(write(fd, contents, size) == static_cast<ssize_t>(size),
          "Could not write configuration test file");
    close(fd);

    string error;
    bool loaded = config->Load(path, error);
    remove(path);
    if (error_message)
        *error_message = error;
    return loaded;
}

static void CheckAppConfig() {
    const char *contents =
        "# Settings may be ordered freely.\n"
        "[logging]\n"
        "file_enabled = 1\n"
        "requests = 0\n"
        "directory = /var/log/esajpip/\n"
        "cache_max_time = -1\n"
        "\n"
        "[jpip]\n"
        "image_directory = /srv/jpip images\n"
        "chunk_size = 4096\n"
        "\n"
        "[listen]\n"
        "address = 127.0.0.1\n"
        "port = 8090\n"
        "\n"
        "[connections]\n"
        "limit = 250\n"
        "timeout = 60\n"
        "initial_timeout = 4\n";

    Config config;
    Check(LoadConfig(contents, &config), "Could not parse INI configuration");
    Check(config.port() == 8090, "Wrong configured port");
    Check(config.address() == "127.0.0.1", "Wrong configured address");
    Check(config.image_directory() == "/srv/jpip images/", "Wrong configured image directory");
    Check(config.log_directory() == "/var/log/esajpip/", "Wrong configured log directory");
    Check(config.max_chunk_size() == 4096, "Wrong configured chunk size");
    Check(config.max_connections() == 250, "Wrong configured connection limit");
    Check(config.initial_timeout() == 4, "Wrong configured initial timeout");
    Check(config.connection_timeout() == 60, "Wrong configured connection timeout");
    Check(config.file_logging() && !config.log_requests(), "Wrong configured logging flags");

    const char *minimal =
        "[listen]\n"
        "port = 8090\n"
        "[jpip]\n"
        "image_directory = /srv/jpip\n"
        "chunk_size = 128\n"
        "[connections]\n"
        "limit = 10\n"
        "[logging]\n";
    Config defaults;
    Check(LoadConfig(minimal, &defaults), "Could not apply configuration defaults");
    Check(defaults.initial_timeout() == 3 && defaults.connection_timeout() == 60,
          "Wrong configuration defaults");

    Config missing_group;
    Check(!LoadConfig("[listen]\nport = 8090\n", &missing_group),
          "Accepted configuration with missing groups");

    Config invalid;
    Check(!LoadConfig("not an INI file", &invalid), "Accepted malformed configuration");

    const char *invalid_port =
        "[listen]\n"
        "port = invalid\n"
        "[jpip]\n"
        "image_directory = /srv/jpip\n"
        "chunk_size = 128\n"
        "[connections]\n"
        "limit = 10\n"
        "[logging]\n";
    string error;
    Config invalid_port_config;
    Check(!LoadConfig(invalid_port, &invalid_port_config, &error) &&
              error.find("port") != string::npos,
          "Did not report an invalid port value");

    const char *invalid_value =
        "[listen]\n"
        "port = 8090\n"
        "[jpip]\n"
        "image_directory = /srv/jpip\n"
        "chunk_size = 127\n"
        "[connections]\n"
        "limit = 10\n"
        "[logging]\n";
    Config invalid_chunk;
    Check(!LoadConfig(invalid_value, &invalid_chunk, &error) &&
              error == "jpip.chunk_size must be at least 128",
          "Did not report an invalid configuration value");

    const char *missing_log_directory =
        "[listen]\n"
        "port = 8090\n"
        "[jpip]\n"
        "image_directory = /srv/jpip\n"
        "chunk_size = 128\n"
        "[connections]\n"
        "limit = 10\n"
        "[logging]\n"
        "file_enabled = 1\n";
    Config invalid_logging;
    Check(!LoadConfig(missing_log_directory, &invalid_logging, &error) &&
              error == "logging.directory must not be empty when file logging is enabled",
          "Accepted file logging without a directory");
}

static InitialRequest Inspect(const char *request) {
    int sockets[2];
    Check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "Could not create initial-request test sockets");
    if (request)
        Check(write(sockets[0], request, strlen(request)) == static_cast<ssize_t>(strlen(request)),
              "Could not write initial test request");

    InitialRequest result = InspectInitialRequest(sockets[1]);
    close(sockets[0]);
    close(sockets[1]);
    return result;
}

static void CheckInitialRequest() {
    Check(Inspect(NULL).state == REQUEST_PENDING, "Rejected an idle connection before its deadline");
    Check(Inspect("G").state == REQUEST_PENDING, "Rejected a partial GET method");
    Check(Inspect("POST").state == REQUEST_REJECTED, "Accepted a non-GET method");
    Check(Inspect("GET /movie.jpx?cnew=http HTTP/1.1\r\n").state == REQUEST_ACCEPTED,
          "Rejected a JHV channel request");
    Check(Inspect("GET /jpip?target=movie.jpx&cnew=http HTTP/1.0\r\n").state == REQUEST_REJECTED,
          "Accepted an HTTP/1.0 channel request");
    Check(Inspect("GET /movie.jpx?xcnew=http HTTP/1.1\r\n").state == REQUEST_REJECTED,
          "Accepted a request without a cnew parameter");
    Check(Inspect("GET /movie.jpx?cnew=http HTTP/2\r\n").state == REQUEST_REJECTED,
          "Accepted an unsupported HTTP version");
    Check(Inspect("GET /movie.jpx?cnew=http HTTP/1.1junk\r\n").state == REQUEST_REJECTED,
          "Accepted an invalid HTTP version token");

    InitialRequest request = Inspect("GET /jpip?stream=2&cid=47 HTTP/1.1\r\n");
    Check(request.state == REQUEST_ACCEPTED && !request.new_channel && request.channel == 47,
          "Could not route an existing channel request");

    InitialRequest close = Inspect("GET /jpip?cclose=47 HTTP/1.1\r\n");
    Check(close.state == REQUEST_ACCEPTED && !close.new_channel && close.channel == 47,
          "Could not route a channel close request");

    InitialRequest close_all = Inspect("GET /jpip?cid=47&cclose=* HTTP/1.1\r\n");
    Check(close_all.state == REQUEST_ACCEPTED && close_all.channel == 47,
          "Could not route an all-channel close request");

    Check(Inspect("GET /jpip?cid=047 HTTP/1.1\r\n").state == REQUEST_REJECTED,
          "Accepted a non-canonical channel ID");
    Check(Inspect("GET /jpip?cid=+47 HTTP/1.1\r\n").state == REQUEST_REJECTED,
          "Accepted a signed channel ID");
    InitialRequest maximum =
            Inspect("GET /jpip?cid=18446744073709551615 HTTP/1.1\r\n");
    Check(maximum.state == REQUEST_ACCEPTED && maximum.channel == UINT64_MAX,
          "Rejected the maximum channel ID");
    Check(Inspect("GET /jpip?cid=18446744073709551616 HTTP/1.1\r\n").state ==
                  REQUEST_REJECTED,
          "Accepted an overflowing channel ID");

    InitialRequest duplicate = Inspect("GET /jpip?cid=46&cid=47 HTTP/1.1\r\n");
    Check(duplicate.state == REQUEST_ACCEPTED && duplicate.channel == 47,
          "Initial inspection did not use the last channel ID");

    InitialRequest mixed = Inspect("GET /jpip?cclose=47&cid=48 HTTP/1.1\r\n");
    Check(mixed.state == REQUEST_ACCEPTED && mixed.channel == 47,
          "Initial inspection did not give cclose routing priority");
}

static void CheckConnectionQueue() {
    ConnectionQueue queue;
    Check(queue.IsValid(), "Could not create a connection queue");
    Check(queue.Push(0), "Could not queue a channel connection");
    Check(!queue.Push(8), "Queued concurrent channel connections");
    int connection;
    Check(queue.Pop(&connection), "Could not retrieve a channel connection");
    Check(connection == 0, "Retrieved the wrong channel connection");
    Check(!queue.Pop(&connection), "Retrieved a channel connection twice");

    Check(queue.Push(9), "Could not queue a channel connection before closing");
    int pending;
    Check(queue.Close(pending), "Connection queue lost its pending connection on close");
    Check(queue.IsClosed(), "Connection queue remained open");
    Check(pending == 9,
          "Connection queue did not return its pending connection");
    Check(!queue.Push(10), "Connection queue accepted a connection after closing");
}

static bool RejectRequest(const string &line) {
    jpip::Request request;
    return !request.Parse(line);
}

static void CheckJHVRequests() {
    jpip::Request channel_request;
    Check(channel_request.Parse(
              "GET /movie.jpx?cnew=http&type=jpp-stream&tid=0&len=512 HTTP/1.1"),
          "Could not parse JHV channel request");
    Check(channel_request.object == "/movie.jpx", "Wrong channel target");
    Check(channel_request.has.cnew, "Missing cnew field");
    Check(channel_request.has.len && channel_request.length_response == 512,
          "Wrong channel response limit");

    jpip::Request target_request;
    Check(target_request.Parse(
              "GET /jpip?target=movie.jpx&cnew=http&len=512 HTTP/1.1"),
          "Could not parse target-form channel request");
    Check(target_request.has.target && target_request.target == "movie.jpx",
          "Missing target field");

    jpip::Request quoted_target_request;
    Check(quoted_target_request.Parse(
              "GET /jpip?target=a\"b.jp2&cnew=http HTTP/1.1\r") &&
              quoted_target_request.target == "a\"b.jp2",
          "Changed the request while preparing it for logging");
    Check(RejectRequest("GET /jpip?cid=7 HTTP/1.1\\r"),
          "Accepted an escaped request-line terminator");

    jpip::Request metadata_request;
    Check(metadata_request.Parse(
              "GET /jpip?stream=0&metareq=[*]!!&len=2000000&cid=7 HTTP/1.1"),
          "Could not parse JHV metadata request");
    Check(metadata_request.has.cid && metadata_request.channel == "7",
          "Missing channel ID");
    Check(metadata_request.target.empty(), "Unexpected target in metadata request");
    Check(metadata_request.has.metareq, "Missing metadata request");
    Check(metadata_request.codestreams == vector<int>(1, 0),
          "Wrong metadata codestream");
    Check(metadata_request.length_response == 2000000,
          "Wrong metadata response limit");

    jpip::Request frame_request;
    Check(frame_request.Parse(
              "GET /jpip?stream=4013&fsiz=4096,4096,closest&rsiz=4096,4096&"
              "roff=0,0&len=2097152&cid=7 HTTP/1.1"),
          "Could not parse JHV frame request");
    Check(frame_request.HasWOI(), "Missing frame window");
    Check(frame_request.codestreams == vector<int>(1, 4013), "Wrong frame codestream");
    Check(frame_request.resolution_size == jpeg2000::Size(4096, 4096),
          "Wrong frame resolution size");
    Check(frame_request.woi_size == jpeg2000::Size(4096, 4096),
          "Wrong frame region size");
    Check(frame_request.woi_position == jpeg2000::Point(0, 0),
          "Wrong frame region offset");
    Check(frame_request.round_direction == jpip::Request::CLOSEST,
          "Wrong frame rounding mode");
    Check(frame_request.length_response == 2097152, "Wrong frame response limit");

    jpip::Request incomplete_window;
    Check(!incomplete_window.Parse("GET /jpip?rsiz=1,1&cid=7 HTTP/1.1") &&
              incomplete_window.resolution_size == jpeg2000::Size(),
          "Accepted a region size without a frame size");

    Check(RejectRequest("GET /jpip?fsiz=abc&cid=7 HTTP/1.1"),
          "Accepted a malformed frame size");
    Check(RejectRequest("GET /jpip?fsiz=1,1,sideways&cid=7 HTTP/1.1"),
          "Accepted an unknown frame-size rounding mode");
    Check(RejectRequest("GET /jpip?roff=abc&cid=7 HTTP/1.1"),
          "Accepted a malformed region offset");
    Check(RejectRequest("GET /jpip?rsiz=abc&cid=7 HTTP/1.1"),
          "Accepted a malformed region size");
    Check(RejectRequest("GET /jpip?stream=abc&cid=7 HTTP/1.1"),
          "Accepted a malformed codestream selector");
    Check(RejectRequest("GET /jpip?context=abc&cid=7 HTTP/1.1"),
          "Accepted a malformed context selector");
    Check(RejectRequest("GET /movie.jpx?cnew=http HTTP/1.0"),
          "Accepted an HTTP/1.0 request");
    Check(RejectRequest("GET /movie.jpx?cnew=http HTTP/1.1junk"),
          "Accepted an invalid HTTP version token");

    jpip::Request model_request;
    Check(model_request.Parse("GET /jpip?stream=0&cid=7&model=M0 HTTP/1.1"),
          "Could not parse terminal cache model");
    Check(model_request.has.model, "Missing terminal cache model");
    Check(model_request.model.size() == 1 &&
              model_request.model[0].bin_class == jpip::DataBinClass::META_DATA &&
              model_request.model[0].id == 0 &&
              model_request.model[0].amount == INT_MAX,
          "Wrong terminal metadata model");

    jpip::Request partial_model_request;
    Check(partial_model_request.Parse(
              "GET /jpip?stream=0&cid=7&model=M0:446 HTTP/1.1"),
          "Could not parse terminal partial cache model");
    Check(partial_model_request.has.model, "Missing terminal partial cache model");
    Check(partial_model_request.model.size() == 1 &&
              partial_model_request.model[0].amount == 446,
          "Wrong terminal partial metadata model");

    jpip::Request empty_resolution_request;
    Check(empty_resolution_request.Parse(
              "GET /jpip?fsiz=0,0&rsiz=1,1&roff=0,0 HTTP/1.1"),
          "Could not parse request with an empty frame size");
    jpeg2000::CodingParameters coding_parameters;
    coding_parameters.size = jpeg2000::Size(4096, 4096);
    coding_parameters.num_levels = 5;
    jpip::WOI woi;
    woi.size = empty_resolution_request.woi_size;
    woi.position = empty_resolution_request.woi_position;
    empty_resolution_request.GetResolution(&coding_parameters, &woi);
    Check(woi.resolution == 0, "Wrong resolution for an empty frame size");
    Check(woi.size == jpeg2000::Size(1, 1), "Changed region for an empty frame size");

    Check(RejectRequest("GET /jpip?model=M-1 HTTP/1.1"),
          "Accepted negative metadata ID");
    Check(RejectRequest("GET /jpip?model=P-1 HTTP/1.1"),
          "Accepted negative precinct ID");
    Check(RejectRequest("GET /jpip?model=M0:-1 HTTP/1.1"),
          "Accepted negative model length");
    Check(RejectRequest("GET /jpip?model= HTTP/1.1"), "Accepted empty cache model");
    Check(RejectRequest("GET /jpip?model=M0% HTTP/1.1"),
          "Accepted truncated cache-model escape");
    Check(RejectRequest("GET /jpip?model=M0%00M1 HTTP/1.1"),
          "Accepted a cache model containing NUL");
    Check(RejectRequest("GET /jpip?len=-1&cid=7 HTTP/1.1"),
          "Accepted negative response length");
    Check(RejectRequest("GET /jpip?len=+1&cid=7 HTTP/1.1"),
          "Accepted a signed response length");
    Check(RejectRequest("GET /jpip?len=2147483648&cid=7 HTTP/1.1"),
          "Accepted an overflowing response length");
    jpip::Request minimum_offset_request;
    Check(minimum_offset_request.Parse(
              "GET /jpip?fsiz=1,1&roff=-2147483648,0&cid=7 HTTP/1.1") &&
              minimum_offset_request.woi_position.x == INT_MIN,
          "Could not parse the minimum signed window offset");

    jpip::Request close_request;
    Check(close_request.Parse("GET /jpip?cclose=7&len=0 HTTP/1.1"),
          "Could not parse JHV close request");
    Check(close_request.has.cclose && close_request.channel == "7", "Missing close field");
    Check(close_request.codestreams.empty(), "Unexpected codestream in close request");

    jpip::Request new_channel_request;
    Check(new_channel_request.Parse("GET /movie.jpx?cnew=http&len=512 HTTP/1.1"),
          "Could not parse a new channel request");
    Check(new_channel_request.channel.empty(), "Unexpected channel ID in new channel request");

    jpip::Request duplicate_channel_request;
    Check(duplicate_channel_request.Parse("GET /jpip?cid=46&cid=47 HTTP/1.1") &&
              duplicate_channel_request.channel == "47",
          "Full parsing did not use the last channel ID");
    jpip::Request close_all_request;
    Check(close_all_request.Parse("GET /jpip?cid=47&cclose=* HTTP/1.1") &&
              close_all_request.has.cclose && close_all_request.channel == "*",
          "Could not parse the all-channel close form");
    jpip::Request close_priority_request;
    Check(close_priority_request.Parse("GET /jpip?cclose=47&cid=48 HTTP/1.1") &&
              close_priority_request.has.cclose && close_priority_request.channel == "47",
          "Full parsing did not give cclose routing priority");

    jpip::Request selection_request;
    Check(selection_request.Parse(
              "GET /jpip?stream=3:1&context=jpxl%3C4-6%3E&cid=7 HTTP/1.1"),
          "Could not parse reduced codestream selectors");
    Check(selection_request.codestreams == vector<int>({3, 2, 1, 4, 5, 6}),
          "Wrong codestream selector ranges");
    Check(RejectRequest(
              "GET /jpip?stream=0:100000&stream=0:100000&cid=7 HTTP/1.1"),
          "Accepted an oversized expanded codestream selection");

    jpip::Request encoded_model_request;
    Check(encoded_model_request.Parse(
              "GET /jpip?model=%5B1-2%5DHm:3,H4:5,P6:7,M8:9&cid=7 HTTP/1.1"),
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

    jpip::Request encoded_target_request;
    Check(encoded_target_request.Parse(
              "GET /jpip?target=movie%20name.jpx&cnew=http HTTP/1.1") &&
              encoded_target_request.target == "movie%20name.jpx",
          "Unexpectedly decoded a target value");
    Check(RejectRequest("GET /jpip?len=12junk&cid=7 HTTP/1.1"),
          "Accepted a response length with trailing data");
}

static void CheckInetAddress() {
    net::InetAddress address("127.0.0.1", 8099);
    Check(address.IsValid(), "A numeric Internet address was not resolved");
    Check(address.GetPath() == "127.0.0.1", "Wrong numeric Internet address");
    Check(address.GetPort() == 8099, "Wrong Internet port");

    net::InetAddress invalid("", 8099);
    Check(!invalid.IsValid(), "An empty Internet address was resolved");
}

static void CheckHTTPResponse() {
    ostringstream out;
    out << http::Response(200, "OK")
        << http::Header("JPIP-cnew", "cid=7,path=jpip,transport=http")
        << http::Header("JPIP-tid", "0")
        << http::Header("Transfer-Encoding", "chunked")
        << http::Header("Content-Type", "image/jpp-stream")
        << http::CRLF;

    Check(out.str() ==
          "HTTP/1.1 200 OK\r\n"
          "JPIP-cnew: cid=7,path=jpip,transport=http\r\n"
          "JPIP-tid: 0\r\n"
          "Transfer-Encoding: chunked\r\n"
          "Content-Type: image/jpp-stream\r\n\r\n",
          "The JHV response headers changed");
}

static void CheckHTTPHeaders() {
    const char *lines[] = {
        "Accept-Encoding: gzip",
        "Accept-Encoding:gzip",
        "Accept-Encoding:\tgzip \t"
    };

    for (const char *line : lines) {
        http::Header header;
        Check(header.Parse(line), "Could not parse an HTTP header");
        Check(header.name == "Accept-Encoding", "Wrong HTTP header name");
        Check(header.value == "gzip", "Wrong HTTP header value");
    }

    http::Header header;
    Check(header.Parse("X-Empty:"), "Could not parse an empty HTTP header");
    Check(header.name == "X-Empty" && header.value.empty(), "Wrong empty HTTP header");
}

static void CheckCacheModel() {
    jpip::CacheModel model;
    const int classes[] = {
        jpip::DataBinClass::META_DATA,
        jpip::DataBinClass::MAIN_HEADER,
        jpip::DataBinClass::TILE_HEADER,
        jpip::DataBinClass::PRECINCT
    };

    for (int bin_class : classes) {
        Check(model.GetDataBin(bin_class, 2, 3) == 0, "Nonempty initial cache model");
        Check(model.AddToDataBin(bin_class, 2, 3, 17) == 17, "Wrong cache-model increment");
        Check(model.GetDataBin(bin_class, 2, 3) == 17, "Wrong cached data-bin length");
        Check(model.AddToDataBin(bin_class, 2, 3, INT_MAX - 18) == INT_MAX - 1,
              "Wrong large cache-model increment");
        Check(model.AddToDataBin(bin_class, 2, 3, 2) == INT_MAX,
              "Cache-model increment overflowed");
        Check(model.AddToDataBin(bin_class, 2, 3, 0, true) == INT_MAX,
              "Incomplete terminal cache model");
    }

    jpip::CacheModel augmented;
    Check(augmented.AugmentDataBin(jpip::DataBinClass::META_DATA, 0, 0, 17) == 17,
          "Did not augment an empty cache-model entry");
    Check(augmented.AugmentDataBin(jpip::DataBinClass::META_DATA, 0, 0, 7) == 17,
          "Reduced an augmented cache-model entry");
    Check(augmented.AugmentDataBin(jpip::DataBinClass::META_DATA, 0, 0, 17) == 17,
          "Accumulated a repeated cache-model descriptor");
    Check(augmented.AugmentDataBin(jpip::DataBinClass::META_DATA, 0, 0, INT_MAX) ==
              INT_MAX,
          "Did not complete an augmented cache-model entry");

    jpip::CacheModel packed;
    packed.AddToDataBin(jpip::DataBinClass::PRECINCT, 0, 0, 0, true);
    packed.Pack();
    Check(packed.GetDataBin(jpip::DataBinClass::PRECINCT, 0, 0) == INT_MAX,
          "Forgot a packed complete precinct");
    packed.SetFullMetadata();
    Check(packed.GetDataBin(jpip::DataBinClass::META_DATA, 0, 7) == INT_MAX,
          "Did not retain the complete metadata state");
}

static void CheckWOIPackets() {
    jpeg2000::CodingParameters coding_parameters;
    coding_parameters.size = jpeg2000::Size(1, 1);
    coding_parameters.num_levels = 0;
    coding_parameters.num_layers = 2;
    coding_parameters.num_components = 2;
    coding_parameters.resolutions.emplace_back(1, 1);
    Check(coding_parameters.FillPrecinctCounts(),
          "Rejected valid WOI coding parameters");

    jpip::WOIComposer composer;
    composer.Reset(&coding_parameters, jpip::WOI(jpeg2000::Point(0, 0), jpeg2000::Size(1, 1), 0));

    int packets = 1;
    while (composer.GetNextPacket(&coding_parameters))
        packets++;
    Check(packets == 4, "WOI navigation repeated the final packet");

    jpeg2000::CodingParameters boundary_parameters;
    boundary_parameters.size = jpeg2000::Size(512, 1);
    boundary_parameters.num_levels = 0;
    boundary_parameters.num_layers = 1;
    boundary_parameters.num_components = 1;
    boundary_parameters.resolutions.emplace_back(256, 1);
    Check(boundary_parameters.FillPrecinctCounts(),
          "Rejected valid precinct-boundary parameters");

    composer.Reset(&boundary_parameters,
                   jpip::WOI(jpeg2000::Point(256, 0),
                             jpeg2000::Size(1, 1), 0));
    Check(composer.GetCurrentPacket().precinct_xy == jpeg2000::Point(1, 0) &&
                  !composer.GetNextPacket(&boundary_parameters),
          "Selected the preceding precinct for an aligned window");

    composer.Reset(&boundary_parameters,
                   jpip::WOI(jpeg2000::Point(0, 0),
                             jpeg2000::Size(257, 1), 0));
    Check(composer.GetCurrentPacket().precinct_xy == jpeg2000::Point(0, 0) &&
                  composer.GetNextPacket(&boundary_parameters) &&
                  composer.GetCurrentPacket().precinct_xy == jpeg2000::Point(1, 0) &&
                  !composer.GetNextPacket(&boundary_parameters),
          "Omitted the final precinct intersecting a window");

    jpeg2000::CodingParameters scaled_parameters;
    scaled_parameters.num_levels = 2;
    scaled_parameters.num_layers = 1;
    scaled_parameters.num_components = 1;
    for (int i = 0; i <= scaled_parameters.num_levels; ++i)
        scaled_parameters.resolutions.emplace_back(1, 1);

    composer.Reset(&scaled_parameters,
                   jpip::WOI(jpeg2000::Point(1, 0),
                             jpeg2000::Size(1, 1), 1));
    Check(composer.GetCurrentPacket().precinct_xy == jpeg2000::Point(0, 0) &&
                  composer.GetNextPacket(&scaled_parameters) &&
                  composer.GetCurrentPacket().resolution == 1 &&
                  composer.GetCurrentPacket().precinct_xy == jpeg2000::Point(1, 0),
          "Mapped a reduced-resolution window to the wrong precinct");

    jpeg2000::CodingParameters limit_parameters;
    limit_parameters.num_levels = 32;
    limit_parameters.num_layers = 1;
    limit_parameters.num_components = 1;
    limit_parameters.resolutions.emplace_back(1, 1);
    composer.Reset(&limit_parameters,
                   jpip::WOI(jpeg2000::Point(0, 0),
                             jpeg2000::Size(1, 1), 0));
    Check(composer.GetCurrentPacket().precinct_xy == jpeg2000::Point(0, 0),
          "Mapped the decomposition-limit window to the wrong precinct");
}

static void CheckProgressionIndexes() {
    jpeg2000::CodingParameters params;
    params.size = jpeg2000::Size(8, 8);
    params.num_levels = 2;
    params.num_layers = 2;
    params.num_components = 3;
    params.resolutions.emplace_back(1, 1);
    params.resolutions.emplace_back(2, 2);
    params.resolutions.emplace_back(4, 4);
    Check(params.FillPrecinctCounts(),
          "Rejected valid progression coding parameters");
    for (const jpeg2000::CodingParameters::Resolution &resolution : params.resolutions)
        Check(resolution.num_precincts == jpeg2000::Size(2, 2),
              "Wrong precomputed precinct count");

    jpeg2000::Packet packet(1, 1, 2, jpeg2000::Size(1, 1));
    params.progression = jpeg2000::CodingParameters::LRCP_PROGRESSION;
    Check(params.GetProgressionIndex(packet) == 59, "Wrong LRCP packet index");
    params.progression = jpeg2000::CodingParameters::RLCP_PROGRESSION;
    Check(params.GetProgressionIndex(packet) == 47, "Wrong RLCP packet index");
    params.progression = jpeg2000::CodingParameters::RPCL_PROGRESSION;
    Check(params.GetProgressionIndex(packet) == 47, "Wrong RPCL packet index");
    params.progression = jpeg2000::CodingParameters::PCRL_PROGRESSION;
    Check(params.GetProgressionIndex(packet) == 69, "Wrong PCRL packet index");
    params.progression = jpeg2000::CodingParameters::CPRL_PROGRESSION;
    Check(params.GetProgressionIndex(packet) == 69, "Wrong CPRL packet index");
    Check(params.GetPrecinctDataBinId(packet) == 23, "Wrong precinct data-bin ID");

    jpeg2000::CodingParameters spatial;
    spatial.size = jpeg2000::Size(11, 9);
    spatial.num_levels = 2;
    spatial.num_layers = 2;
    spatial.num_components = 3;
    spatial.resolutions.emplace_back(1, 2);
    spatial.resolutions.emplace_back(2, 1);
    spatial.resolutions.emplace_back(1, 4);
    Check(spatial.FillPrecinctCounts(),
          "Rejected valid spatial progression parameters");

    int num_packets = 0;
    for (const jpeg2000::CodingParameters::Resolution &resolution : spatial.resolutions)
        num_packets += resolution.num_precincts.x * resolution.num_precincts.y;
    num_packets *= spatial.num_components * spatial.num_layers;

    int expected = 0;
    spatial.progression = jpeg2000::CodingParameters::PCRL_PROGRESSION;
    for (int y = 0; y < spatial.size.y; ++y) {
        for (int x = 0; x < spatial.size.x; ++x) {
            for (int c = 0; c < spatial.num_components; ++c) {
                for (int r = 0; r <= spatial.num_levels; ++r) {
                    int step_x = spatial.resolutions[r].precinct_size.x <<
                                 (spatial.num_levels - r);
                    int step_y = spatial.resolutions[r].precinct_size.y <<
                                 (spatial.num_levels - r);
                    if (x % step_x != 0 || y % step_y != 0)
                        continue;
                    for (int l = 0; l < spatial.num_layers; ++l) {
                        packet = jpeg2000::Packet(l, r, c,
                                jpeg2000::Point(x / step_x, y / step_y));
                        Check(spatial.GetProgressionIndex(packet) == expected++,
                              "PCRL packet sequence does not follow the reference grid");
                    }
                }
            }
        }
    }
    Check(expected == num_packets, "PCRL packet sequence is incomplete");

    expected = 0;
    spatial.progression = jpeg2000::CodingParameters::CPRL_PROGRESSION;
    for (int c = 0; c < spatial.num_components; ++c) {
        for (int y = 0; y < spatial.size.y; ++y) {
            for (int x = 0; x < spatial.size.x; ++x) {
                for (int r = 0; r <= spatial.num_levels; ++r) {
                    int step_x = spatial.resolutions[r].precinct_size.x <<
                                 (spatial.num_levels - r);
                    int step_y = spatial.resolutions[r].precinct_size.y <<
                                 (spatial.num_levels - r);
                    if (x % step_x != 0 || y % step_y != 0)
                        continue;
                    for (int l = 0; l < spatial.num_layers; ++l) {
                        packet = jpeg2000::Packet(l, r, c,
                                jpeg2000::Point(x / step_x, y / step_y));
                        Check(spatial.GetProgressionIndex(packet) == expected++,
                              "CPRL packet sequence does not follow the reference grid");
                    }
                }
            }
        }
    }
    Check(expected == num_packets, "CPRL packet sequence is incomplete");
}

static void CheckResolutionSelection() {
    jpeg2000::CodingParameters params;
    params.size = jpeg2000::Size(4097, 4095);
    params.num_levels = 5;
    jpeg2000::Size selected;

    Check(params.GetClosestResolution(jpeg2000::Size(2050, 2048), &selected) == 4 &&
              selected == jpeg2000::Size(2049, 2048),
          "Wrong closest resolution size");
    Check(params.GetRoundUpResolution(jpeg2000::Size(2048, 2048), &selected) == 4 &&
              selected == jpeg2000::Size(2049, 2048),
          "Wrong round-up resolution size");
    Check(params.GetRoundDownResolution(jpeg2000::Size(2048, 2048), &selected) == 3 &&
              selected == jpeg2000::Size(1025, 1024),
          "Wrong round-down resolution size");

    params.size = jpeg2000::Size(INT_MAX, INT_MAX);
    params.num_levels = 32;
    Check(params.GetRoundDownResolution(jpeg2000::Size(1, 1), &selected) == 1 &&
              selected == jpeg2000::Size(1, 1),
          "Wrong resolution size at the decomposition limit");

    jpip::Request request;
    request.resolution_size = jpeg2000::Size(INT_MAX - 1, INT_MAX - 1);
    jpip::WOI woi;
    woi.position = jpeg2000::Point(INT_MAX - 2, INT_MAX - 2);
    woi.size = jpeg2000::Size(1, 1);
    request.GetResolution(&params, &woi);
    Check(woi.position == jpeg2000::Point(INT_MAX - 1, INT_MAX - 1) &&
              woi.size == jpeg2000::Size(2, 2),
          "Window scaling overflowed");
}

static void CheckJPIPMessages() {
    char buf[128];
    data::File file;
    jpip::DataBinWriter writer;
    writer.SetBuffer(buf, sizeof buf);
    writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                 data::FileSegment::Null, true);
    writer.WriteEOR(jpip::EOR::WINDOW_DONE);

    const unsigned char expected[] = {0x70, 0x06, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00};
    Check(writer.GetCount() == sizeof expected, "Wrong JPIP message length");
    for (size_t i = 0; i < sizeof expected; ++i)
        Check(static_cast<unsigned char>(buf[i]) == expected[i], "The JPIP message format changed");
}

static void CheckDataBinCapacity() {
    char tiny_buf[2];
    jpip::DataBinWriter tiny_writer;
    tiny_writer.SetBuffer(tiny_buf, sizeof tiny_buf);
    Check(!tiny_writer.WriteEOR(jpip::EOR::WINDOW_DONE),
          "Accepted an EOR message larger than the buffer");
    char eor_buf[3];
    tiny_writer.SetBuffer(eor_buf, sizeof eor_buf);
    Check(tiny_writer.WriteEOR(jpip::EOR::WINDOW_DONE),
          "Did not retry the EOR message in a new buffer");

    char buf[32];
    data::File file;
    jpip::DataBinWriter writer;
    writer.SetBuffer(buf, sizeof buf);
    Check(writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                       data::FileSegment(0, UINT64_MAX), true) ==
              jpip::DataBinWriter::Result::FULL,
          "Reported an oversized data-bin segment as written");
    Check(writer.GetCount() == 0, "Left a partial data-bin message in the buffer");

    char path[] = "/tmp/esajpip-short-read-XXXXXX";
    int fd = mkstemp(path);
    Check(fd >= 0, "Could not create short payload file");
    Check(write(fd, "x", 1) == 1, "Could not write short payload file");
    close(fd);

    Check(file.Open(path), "Could not open short payload file");
    remove(path);
    writer.SetBuffer(buf, sizeof buf);
    Check(writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                       data::FileSegment(0, 2), true) ==
              jpip::DataBinWriter::Result::FAILED,
          "Reported a short source-file read as written");
}

static void CheckCoalescedJPIPMessages() {
    char path[] = "/tmp/esajpip-protocol-XXXXXX";
    int fd = mkstemp(path);
    Check(fd >= 0, "Could not create payload file");
    char payload[200];
    for (size_t i = 0; i < sizeof payload; ++i)
        payload[i] = i + 1;
    Check(write(fd, payload, sizeof payload) == sizeof payload, "Could not write payload file");
    close(fd);

    data::File file;
    Check(file.Open(path), "Could not open payload file");
    remove(path);

    char buf[128];
    jpip::DataBinWriter writer;
    writer.SetBuffer(buf, sizeof buf);
    writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                 data::FileSegment(0, 2), false);
    writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 2, file,
                 data::FileSegment(2, 3), true);
    writer.WriteEOR(jpip::EOR::WINDOW_DONE);

    const unsigned char expected[] = {0x70, 0x06, 0x00, 0x00, 0x05, 1, 2, 3, 4, 5, 0x00, 0x02, 0x00};
    Check(writer.GetCount() == sizeof expected, "Wrong coalesced JPIP message length");
    for (size_t i = 0; i < sizeof expected; ++i)
        Check(static_cast<unsigned char>(buf[i]) == expected[i], "Wrong coalesced JPIP message");

    char growing_buf[256];
    jpip::DataBinWriter growing_writer;
    growing_writer.SetBuffer(growing_buf, sizeof growing_buf);
    growing_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                         data::FileSegment(0, 100), false);
    growing_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 100, file,
                         data::FileSegment(100, 50), true);
    growing_writer.WriteEOR(jpip::EOR::WINDOW_DONE);

    Check(growing_writer.GetCount() == 159, "Wrong growing JPIP message length");
    const unsigned char growing_header[] = {0x70, 0x06, 0x00, 0x00, 0x81, 0x16};
    for (size_t i = 0; i < sizeof growing_header; ++i)
        Check(static_cast<unsigned char>(growing_buf[i]) == growing_header[i], "Wrong growing JPIP header");
    for (size_t i = 0; i < 150; ++i)
        Check(growing_buf[sizeof growing_header + i] == payload[i], "Wrong shifted JPIP payload");

    char tight_buf[155];
    jpip::DataBinWriter tight_writer;
    tight_writer.SetBuffer(tight_buf, sizeof tight_buf);
    Check(tight_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                             data::FileSegment(0, 100), false) ==
              jpip::DataBinWriter::Result::WRITTEN,
          "Rejected the complete JPIP message");
    Check(tight_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 100, file,
                             data::FileSegment(100, 50), true) ==
              jpip::DataBinWriter::Result::FULL,
          "Coalesced a message without space for its header");

    Check(tight_writer.GetCount() == 105, "Did not preserve the complete JPIP message");
    const unsigned char tight_header[] = {0x60, 0x06, 0x00, 0x00, 0x64};
    for (size_t i = 0; i < sizeof tight_header; ++i)
        Check(static_cast<unsigned char>(tight_buf[i]) == tight_header[i],
              "Corrupted the complete JPIP message");
    for (size_t i = 0; i < 100; ++i)
        Check(tight_buf[sizeof tight_header + i] == payload[i],
              "Corrupted the complete JPIP payload");

    char retry_buf[64];
    tight_writer.SetBuffer(retry_buf, sizeof retry_buf);
    Check(tight_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 100, file,
                             data::FileSegment(100, 50), true) ==
              jpip::DataBinWriter::Result::WRITTEN,
          "Did not retry the deferred JPIP message");
    Check(tight_writer.GetCount() == 53, "Wrong deferred JPIP message length");
    const unsigned char retry_header[] = {0x30, 0x64, 0x32};
    for (size_t i = 0; i < sizeof retry_header; ++i)
        Check(static_cast<unsigned char>(retry_buf[i]) == retry_header[i],
              "Corrupted the deferred JPIP message");
    for (size_t i = 0; i < 50; ++i)
        Check(retry_buf[sizeof retry_header + i] == payload[i + 100],
              "Corrupted the deferred JPIP payload");

    char class_buf[128];
    jpip::DataBinWriter class_writer;
    class_writer.SetBuffer(class_buf, sizeof class_buf);
    class_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                       data::FileSegment(0, 2), true);
    class_writer.Write(jpip::DataBinClass::META_DATA, 0, 0, 0, file,
                       data::FileSegment(2, 3), true);
    class_writer.WriteEOR(jpip::EOR::WINDOW_DONE);

    const unsigned char class_expected[] = {0x70, 0x06, 0x00, 0x00, 0x02, 1, 2,
                                            0x50, 0x08, 0x00, 0x03, 3, 4, 5,
                                            0x00, 0x02, 0x00};
    Check(class_writer.GetCount() == sizeof class_expected, "Wrong mixed-class JPIP message length");
    for (size_t i = 0; i < sizeof class_expected; ++i)
        Check(static_cast<unsigned char>(class_buf[i]) == class_expected[i], "Wrong mixed-class JPIP message");

    char stream_buf[128];
    jpip::DataBinWriter stream_writer;
    stream_writer.SetBuffer(stream_buf, sizeof stream_buf);
    stream_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                        data::FileSegment(0, 1), true);
    stream_writer.Write(jpip::DataBinClass::MAIN_HEADER, 1, 0, 0, file,
                        data::FileSegment(1, 1), true);

    const unsigned char stream_expected[] = {0x70, 0x06, 0x00, 0x00, 0x01, 1,
                                             0x70, 0x06, 0x01, 0x00, 0x01, 2};
    Check(stream_writer.GetCount() == sizeof stream_expected,
          "Wrong mixed-codestream JPIP message length");
    for (size_t i = 0; i < sizeof stream_expected; ++i)
        Check(static_cast<unsigned char>(stream_buf[i]) == stream_expected[i],
              "Wrong mixed-codestream JPIP message");

    char exact_buf[7];
    jpip::DataBinWriter exact_writer;
    exact_writer.SetBuffer(exact_buf, sizeof exact_buf);
    exact_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                       data::FileSegment(0, 2), true);

    const unsigned char exact_expected[] = {0x70, 0x06, 0x00, 0x00, 0x02, 1, 2};
    Check(exact_writer.GetCount() == sizeof exact_expected, "Did not fill the JPIP buffer exactly");
    for (size_t i = 0; i < sizeof exact_expected; ++i)
        Check(static_cast<unsigned char>(exact_buf[i]) == exact_expected[i], "Wrong exact-size JPIP message");
}

static void CheckMetadataPlaceHolder() {
    char path[] = "/tmp/esajpip-placeholder-XXXXXX";
    int fd = mkstemp(path);
    Check(fd >= 0, "Could not create box-header file");
    const unsigned char header[] = {0x00, 0x00, 0x00, 0x10, 'a', 's', 'o', 'c'};
    Check(write(fd, header, sizeof header) == sizeof header, "Could not write box header");
    close(fd);

    data::File file;
    Check(file.Open(path), "Could not open box-header file");
    remove(path);

    char buf[64];
    jpip::DataBinWriter writer;
    jpeg2000::PlaceHolder place_holder(7, false,
                                       data::FileSegment(0, sizeof header));
    writer.SetBuffer(buf, sizeof buf);
    writer.WritePlaceHolder(jpip::DataBinClass::META_DATA, 0, 0, 0, file,
                            place_holder, true);

    const unsigned char expected[] = {
        0x70, 0x08, 0x00, 0x00, 0x1c,
        0x00, 0x00, 0x00, 0x1c, 'p', 'h', 'l', 'd',
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x10, 'a', 's', 'o', 'c'
    };
    Check(writer.GetCount() == sizeof expected, "Wrong metadata place-holder length");
    for (size_t i = 0; i < sizeof expected; ++i)
        Check(static_cast<unsigned char>(buf[i]) == expected[i], "Wrong metadata place-holder");

    char exact_buf[sizeof expected];
    jpip::DataBinWriter exact_writer;
    exact_writer.SetBuffer(exact_buf, sizeof exact_buf);
    exact_writer.WritePlaceHolder(jpip::DataBinClass::META_DATA, 0, 0, 0, file,
                                  place_holder, true);

    Check(exact_writer.GetCount() == sizeof expected, "Did not fill the place-holder buffer exactly");
    for (size_t i = 0; i < sizeof expected; ++i)
        Check(static_cast<unsigned char>(exact_buf[i]) == expected[i], "Wrong exact-size metadata place-holder");
}

int main() {
    CheckAppConfig();
    CheckInitialRequest();
    CheckConnectionQueue();
    CheckInetAddress();
    CheckJHVRequests();
    CheckCacheModel();
    CheckHTTPHeaders();
    CheckHTTPResponse();
    CheckWOIPackets();
    CheckProgressionIndexes();
    CheckResolutionSelection();
    CheckJPIPMessages();
    CheckDataBinCapacity();
    CheckCoalescedJPIPMessages();
    CheckMetadataPlaceHolder();
    return EXIT_SUCCESS;
}
