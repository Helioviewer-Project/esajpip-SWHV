#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "data/file.h"
#include "data/file_segment.h"
#include "http/header.h"
#include "http/request.h"
#include "http/response.h"
#include "jpip/databin_writer.h"
#include "jpip/jpip.h"
#include "jpip/request.h"

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

    Check(req.Parse("GET /jpip?cclose=7&len=0 HTTP/1.1"), "Could not parse JHV close request");
    Check(req.mask.items.cclose && req.parameters["cclose"] == "7", "Missing close field");
    Check(req.codestreams.empty(), "A reused request retained its codestreams");
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

static void CheckJPIPMessages() {
    char buf[16];
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

int main() {
    CheckJHVRequests();
    CheckHTTPResponse();
    CheckJPIPMessages();
    return EXIT_SUCCESS;
}
