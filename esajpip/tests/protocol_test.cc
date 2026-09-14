#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "data/file.h"
#include "data/file_segment.h"
#include "http/header.h"
#include "http/request.h"
#include "http/response.h"
#include "jpeg2000/place_holder.h"
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

static void CheckJHVRequests() {
    jpip::Request req;

    Check(req.Parse("GET /movie.jpx?cnew=http&type=jpp-stream&tid=0&len=512 HTTP/1.1"),
          "Could not parse JHV channel request");
    Check(req.object == "/movie.jpx", "Wrong channel target");
    Check(req.mask.items.cnew && req.parameters["cnew"] == "http", "Missing cnew field");
    Check(req.mask.items.len && req.length_response == 512, "Wrong channel response limit");

    Check(req.Parse("GET /jpip?stream=0&metareq=[*]!!&len=2000000&cid=7 HTTP/1.1"),
          "Could not parse JHV metadata request");
    Check(req.mask.items.cid && req.parameters["cid"] == "7", "Missing channel ID");
    Check(req.mask.items.metareq, "Missing metadata request");
    Check(req.codestreams == vector<int>(1, 0), "Wrong metadata codestream");
    Check(req.length_response == 2000000, "Wrong metadata response limit");

    Check(req.Parse("GET /jpip?stream=4013&fsiz=4096,4096,closest&rsiz=4096,4096&roff=0,0&len=2097152&cid=7 HTTP/1.1"),
          "Could not parse JHV frame request");
    Check(req.mask.HasWOI(), "Missing frame window");
    Check(req.codestreams == vector<int>(1, 4013), "Wrong frame codestream");
    Check(req.resolution_size == jpeg2000::Size(4096, 4096), "Wrong frame resolution size");
    Check(req.woi_size == jpeg2000::Size(4096, 4096), "Wrong frame region size");
    Check(req.woi_position == jpeg2000::Point(0, 0), "Wrong frame region offset");
    Check(req.round_direction == jpip::Request::CLOSEST, "Wrong frame rounding mode");
    Check(req.length_response == 2097152, "Wrong frame response limit");

    Check(req.Parse("GET /jpip?stream=0&cid=7&model=M0 HTTP/1.1"),
          "Could not parse terminal cache model");
    Check(req.mask.items.model, "Missing terminal cache model");
    Check(req.cache_model.GetMetadata(0) == INT_MAX, "Wrong terminal metadata model");

    Check(req.Parse("GET /jpip?stream=0&cid=7&model=M0:446 HTTP/1.1"),
          "Could not parse terminal partial cache model");
    Check(req.mask.items.model, "Missing terminal partial cache model");
    Check(req.cache_model.GetMetadata(0) == 446, "Wrong terminal partial metadata model");

    Check(req.Parse("GET /jpip?fsiz=0,0&rsiz=1,1&roff=0,0 HTTP/1.1"),
          "Could not parse request with an empty frame size");
    jpeg2000::CodingParameters coding_parameters;
    coding_parameters.size = jpeg2000::Size(4096, 4096);
    coding_parameters.num_levels = 5;
    jpip::WOI woi;
    woi.size = req.woi_size;
    woi.position = req.woi_position;
    req.GetResolution(&coding_parameters, &woi);
    Check(woi.resolution == 0, "Wrong resolution for an empty frame size");
    Check(woi.size == jpeg2000::Size(1, 1), "Changed region for an empty frame size");

    Check(req.Parse("GET /jpip?model=M-1 HTTP/1.1"), "Could not parse request with negative metadata ID");
    Check(!req.mask.items.model, "Accepted negative metadata ID");
    Check(req.Parse("GET /jpip?model=P-1 HTTP/1.1"), "Could not parse request with negative precinct ID");
    Check(!req.mask.items.model, "Accepted negative precinct ID");
    Check(req.Parse("GET /jpip?model=M0:-1 HTTP/1.1"), "Could not parse request with negative model length");
    Check(!req.mask.items.model, "Accepted negative model length");

    Check(req.Parse("GET /jpip?cclose=7&len=0 HTTP/1.1"), "Could not parse JHV close request");
    Check(req.mask.items.cclose && req.parameters["cclose"] == "7", "Missing close field");
    Check(req.codestreams.empty(), "A reused request retained its codestreams");
}

static void CheckInetAddress() {
    net::InetAddress address("127.0.0.1", 8099);
    Check(address.GetPath() == "127.0.0.1", "Wrong numeric Internet address");
    Check(address.GetPort() == 8099, "Wrong Internet port");
}

static void CheckHTTPResponse() {
    ostringstream out;
    out << http::Response(200)
        << http::Header("JPIP-cnew", "cid=7,path=jpip,transport=http")
        << http::Header::TransferEncoding("chunked")
        << http::Header::ContentType("image/jpp-stream")
        << http::Protocol::CRLF;

    Check(out.str() ==
          "HTTP/1.1 200 OK\r\n"
          "JPIP-cnew: cid=7,path=jpip,transport=http\r\n"
          "Transfer-Encoding: chunked\r\n"
          "Content-Type: image/jpp-stream\r\n\r\n",
          "The JHV response headers changed");
}

static void CheckWOIPackets() {
    jpeg2000::CodingParameters coding_parameters;
    coding_parameters.size = jpeg2000::Size(1, 1);
    coding_parameters.num_levels = 0;
    coding_parameters.num_layers = 2;
    coding_parameters.num_components = 2;
    coding_parameters.precinct_size.emplace_back(1, 1);

    jpip::WOIComposer composer;
    composer.Reset(&coding_parameters, jpip::WOI(jpeg2000::Point(0, 0), jpeg2000::Size(1, 1), 0));

    int packets = 1;
    while (composer.GetNextPacket(&coding_parameters))
        packets++;
    Check(packets == 4, "WOI navigation repeated the final packet");
}

static void CheckJPIPMessages() {
    char buf[128];
    data::File file;
    jpip::DataBinWriter writer;
    writer.SetBuffer(buf, sizeof buf)
          .SetCodestream(0)
          .SetDataBinClass(jpip::DataBinClass::MAIN_HEADER)
          .Write(0, 0, file, data::FileSegment::Null, true)
          .WriteEOR(jpip::EOR::WINDOW_DONE);

    const unsigned char expected[] = {0x70, 0x06, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00};
    Check(writer.GetCount() == sizeof expected, "Wrong JPIP message length");
    for (size_t i = 0; i < sizeof expected; ++i)
        Check(static_cast<unsigned char>(buf[i]) == expected[i], "The JPIP message format changed");
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
    writer.SetBuffer(buf, sizeof buf)
          .SetCodestream(0)
          .SetDataBinClass(jpip::DataBinClass::MAIN_HEADER)
          .Write(0, 0, file, data::FileSegment(0, 2), false)
          .Write(0, 2, file, data::FileSegment(2, 3), true)
          .WriteEOR(jpip::EOR::WINDOW_DONE);

    const unsigned char expected[] = {0x70, 0x06, 0x00, 0x00, 0x05, 1, 2, 3, 4, 5, 0x00, 0x02, 0x00};
    Check(writer.GetCount() == sizeof expected, "Wrong coalesced JPIP message length");
    for (size_t i = 0; i < sizeof expected; ++i)
        Check(static_cast<unsigned char>(buf[i]) == expected[i], "Wrong coalesced JPIP message");

    char growing_buf[256];
    jpip::DataBinWriter growing_writer;
    growing_writer.SetBuffer(growing_buf, sizeof growing_buf)
                  .SetCodestream(0)
                  .SetDataBinClass(jpip::DataBinClass::MAIN_HEADER)
                  .Write(0, 0, file, data::FileSegment(0, 100), false)
                  .Write(0, 100, file, data::FileSegment(100, 50), true)
                  .WriteEOR(jpip::EOR::WINDOW_DONE);

    Check(growing_writer.GetCount() == 159, "Wrong growing JPIP message length");
    const unsigned char growing_header[] = {0x70, 0x06, 0x00, 0x00, 0x81, 0x16};
    for (size_t i = 0; i < sizeof growing_header; ++i)
        Check(static_cast<unsigned char>(growing_buf[i]) == growing_header[i], "Wrong growing JPIP header");
    for (size_t i = 0; i < 150; ++i)
        Check(growing_buf[sizeof growing_header + i] == payload[i], "Wrong shifted JPIP payload");

    char class_buf[128];
    jpip::DataBinWriter class_writer;
    class_writer.SetBuffer(class_buf, sizeof class_buf)
                .SetCodestream(0)
                .SetDataBinClass(jpip::DataBinClass::MAIN_HEADER)
                .Write(0, 0, file, data::FileSegment(0, 2), true)
                .SetDataBinClass(jpip::DataBinClass::META_DATA)
                .Write(0, 0, file, data::FileSegment(2, 3), true)
                .WriteEOR(jpip::EOR::WINDOW_DONE);

    const unsigned char class_expected[] = {0x70, 0x06, 0x00, 0x00, 0x02, 1, 2,
                                            0x50, 0x08, 0x00, 0x03, 3, 4, 5,
                                            0x00, 0x02, 0x00};
    Check(class_writer.GetCount() == sizeof class_expected, "Wrong mixed-class JPIP message length");
    for (size_t i = 0; i < sizeof class_expected; ++i)
        Check(static_cast<unsigned char>(class_buf[i]) == class_expected[i], "Wrong mixed-class JPIP message");
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
    jpeg2000::PlaceHolder place_holder(7, false, data::FileSegment(0, sizeof header), 8);
    writer.SetBuffer(buf, sizeof buf)
          .SetCodestream(0)
          .SetDataBinClass(jpip::DataBinClass::META_DATA)
          .WritePlaceHolder(0, 0, file, place_holder, true);

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
}

int main() {
    CheckInetAddress();
    CheckJHVRequests();
    CheckHTTPResponse();
    CheckWOIPackets();
    CheckJPIPMessages();
    CheckCoalescedJPIPMessages();
    CheckMetadataPlaceHolder();
    return EXIT_SUCCESS;
}
