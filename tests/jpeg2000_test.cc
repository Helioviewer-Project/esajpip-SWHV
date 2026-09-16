#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

#include "jpeg2000/file_manager.h"
#include "jpip/databin_server.h"
#include "jpip/request.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static void Append16(vector<unsigned char> &data, uint16_t value) {
    data.push_back(value >> 8);
    data.push_back(value);
}

static void Append32(vector<unsigned char> &data, uint32_t value) {
    data.push_back(value >> 24);
    data.push_back(value >> 16);
    data.push_back(value >> 8);
    data.push_back(value);
}

static void Set32(vector<unsigned char> &data, size_t offset, uint32_t value) {
    data[offset] = value >> 24;
    data[offset + 1] = value >> 16;
    data[offset + 2] = value >> 8;
    data[offset + 3] = value;
}

static void Append64(vector<unsigned char> &data, uint64_t value) {
    Append32(data, value >> 32);
    Append32(data, value);
}

static void AppendBox(vector<unsigned char> &file, uint32_t type,
                      const vector<unsigned char> &contents) {
    Append32(file, contents.size() + 8);
    Append32(file, type);
    file.insert(file.end(), contents.begin(), contents.end());
}

static vector<unsigned char> MakeCodestream(uint8_t progression = 0, uint8_t sampling = 1,
                                            uint32_t image_width = 1, uint32_t tile_width = 1,
                                            uint16_t tile_index = 0,
                                            uint16_t quality_layers = 1,
                                            uint8_t tile_parts = 1,
                                            uint8_t packets_per_tile_part = 1) {
    vector<unsigned char> codestream;
    Append16(codestream, 0xFF4F); // SOC

    Append16(codestream, 0xFF51); // SIZ
    Append16(codestream, 41);
    Append16(codestream, 0);      // Rsiz
    Append32(codestream, image_width); // Xsiz
    Append32(codestream, 1);      // Ysiz
    Append32(codestream, 0);      // XOsiz
    Append32(codestream, 0);      // YOsiz
    Append32(codestream, tile_width); // XTsiz
    Append32(codestream, 1);      // YTsiz
    Append32(codestream, 0);      // XTOsiz
    Append32(codestream, 0);      // YTOsiz
    Append16(codestream, 1);      // Csiz
    codestream.push_back(7);      // Ssiz
    codestream.push_back(sampling); // XRsiz
    codestream.push_back(sampling); // YRsiz

    Append16(codestream, 0xFF52); // COD
    Append16(codestream, 12);
    codestream.push_back(0);      // Scod
    codestream.push_back(progression);
    Append16(codestream, quality_layers);
    codestream.push_back(0);      // MCT
    codestream.push_back(0);      // decomposition levels
    codestream.push_back(0);      // code-block width
    codestream.push_back(0);      // code-block height
    codestream.push_back(0);      // code-block style
    codestream.push_back(1);      // reversible transform

    Append16(codestream, 0xFF5C); // QCD
    Append16(codestream, 3);
    codestream.push_back(0);

    for (int tile_part = 0; tile_part < tile_parts; ++tile_part) {
        Append16(codestream, 0xFF90); // SOT
        Append16(codestream, 10);
        Append16(codestream, tile_index);
        Append32(codestream, 19 + 2 * packets_per_tile_part);
        codestream.push_back(tile_part);
        codestream.push_back(tile_parts);

        Append16(codestream, 0xFF58); // PLT
        Append16(codestream, 3 + packets_per_tile_part);
        codestream.push_back(0);      // Zplt
        for (int packet = 0; packet < packets_per_tile_part; ++packet)
            codestream.push_back(1);  // one-byte packet
        Append16(codestream, 0xFF93); // SOD
        for (int packet = 0; packet < packets_per_tile_part; ++packet)
            codestream.push_back(0);  // packet data
    }
    Append16(codestream, 0xFFD9); // EOC
    return codestream;
}

static vector<unsigned char> MakeJP2(const vector<unsigned char> &codestream) {
    vector<unsigned char> file;
    AppendBox(file, 0x6A703263, codestream); // jp2c
    return file;
}

static vector<unsigned char> MakeEmbeddedJPX(const vector<unsigned char> &codestream,
                                             const vector<unsigned char> &second = {}) {
    vector<unsigned char> file;
    AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch
    AppendBox(file, 0x6A703263, codestream);              // jp2c
    if (!second.empty()) {
        AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch
        AppendBox(file, 0x6A703263, second);                  // jp2c
    }
    return file;
}

static vector<unsigned char> MakeLinkedJPX(const string &linked_path,
                                            uint32_t codestream_length,
                                            uint16_t reference_count = 1) {
    vector<unsigned char> file;
    AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch

    vector<unsigned char> fragment;
    Append16(fragment, 1);                 // one fragment
    Append64(fragment, 8);                 // jp2c contents
    Append32(fragment, codestream_length);
    Append16(fragment, 1);                 // data reference
    vector<unsigned char> fragment_box;
    AppendBox(fragment_box, 0x666C7374, fragment); // flst
    AppendBox(file, 0x6674626C, fragment_box);     // ftbl

    vector<unsigned char> url;
    Append32(url, 0); // version and flags
    string location = "file://" + linked_path;
    url.insert(url.end(), location.begin(), location.end());
    url.push_back(0);
    vector<unsigned char> references;
    Append16(references, reference_count);
    AppendBox(references, 0x75726C20, url); // url
    AppendBox(file, 0x6474626C, references); // dtbl
    return file;
}

static void WriteFile(const string &path, const vector<unsigned char> &data) {
    ofstream out(path.c_str(), std::ios::binary);
    Check(static_cast<bool>(out), "Could not create JPEG 2000 test file");
    out.write(reinterpret_cast<const char *>(data.data()), data.size());
    Check(static_cast<bool>(out), "Could not write JPEG 2000 test file");
}

static bool OpenImage(const string &directory, const string &name,
                      jpeg2000::FileManager *manager) {
    Check(manager->Init(directory), "Could not initialize the file manager");
    string request_name = name;
    return manager->OpenImage(request_name);
}

int main() {
    char directory_template[] = "/tmp/esajpip-jpeg2000-XXXXXX";
    char *directory_name = mkdtemp(directory_template);
    Check(directory_name != NULL, "Could not create JPEG 2000 test directory");
    string directory = string(directory_name) + "/";

    vector<unsigned char> codestream = MakeCodestream();
    vector<unsigned char> jp2 = MakeJP2(codestream);
    WriteFile(directory + "image.jp2", jp2);
    WriteFile(directory + "pcrl.jp2", MakeJP2(MakeCodestream(3)));
    WriteFile(directory + "cprl.jp2", MakeJP2(MakeCodestream(4)));
    WriteFile(directory + "unsupported-pcrl.jp2", MakeJP2(MakeCodestream(3, 2)));
    WriteFile(directory + "multi-tile.jp2", MakeJP2(MakeCodestream(0, 1, 2, 1)));
    WriteFile(directory + "bad-tile-index.jp2", MakeJP2(MakeCodestream(0, 1, 1, 1, 1)));
    WriteFile(directory + "64-tile-parts.jp2",
              MakeJP2(MakeCodestream(0, 1, 1, 1, 0, 64, 64)));
    WriteFile(directory + "65-tile-parts.jp2",
              MakeJP2(MakeCodestream(0, 1, 1, 1, 0, 65, 65)));
    WriteFile(directory + "default-precincts.jp2",
              MakeJP2(MakeCodestream(0, 1, 65537, 65537, 0, 1, 1, 3)));
    vector<unsigned char> nonzero_origin = MakeCodestream(0, 1, 2, 2);
    Set32(nonzero_origin, 16, 1); // XOsiz
    WriteFile(directory + "nonzero-origin.jp2", MakeJP2(nonzero_origin));
    WriteFile(directory + "embedded.jpx", MakeEmbeddedJPX(codestream));
    WriteFile(directory + "embedded-two.jpx",
              MakeEmbeddedJPX(codestream,
                              MakeCodestream(0, 1, 2, 2, 0, 2, 1, 2)));
    WriteFile(directory + "linked.jpx",
              MakeLinkedJPX(directory + "image.jp2", codestream.size()));

    jpeg2000::FileManager manager;
    Check(OpenImage(directory, "image.jp2", &manager), "Could not parse valid JP2");
    Check(manager.GetImage()->GetNumCodestreams() == 1, "Wrong JP2 codestream count");

    data::File *file = manager.GetFile(directory + "image.jp2");
    Check(file != NULL, "Could not reopen valid JP2");
    data::FileSegment packet;
    Check(manager.GetImage()->GetPacket(file, 0,
                                        jpeg2000::Packet(0, 0, 0, jpeg2000::Point()),
                                        &packet),
          "Could not index valid JP2 packet");
    Check(packet.length == 1, "Wrong JP2 packet length");

    for (const char *name : {"pcrl.jp2", "cprl.jp2"}) {
        jpeg2000::FileManager progression_manager;
        Check(OpenImage(directory, name, &progression_manager),
              "Could not parse spatially progressive JP2");
        data::File *progression_file = progression_manager.GetFile(directory + name);
        Check(progression_file != NULL, "Could not reopen spatially progressive JP2");
        Check(progression_manager.GetImage()->GetPacket(
                      progression_file, 0,
                      jpeg2000::Packet(0, 0, 0, jpeg2000::Point()), &packet),
              "Could not index spatially progressive JP2 packet");
        Check(packet.length == 1, "Wrong spatially progressive packet length");
    }
    jpeg2000::FileManager unsupported_progression_manager;
    Check(!OpenImage(directory, "unsupported-pcrl.jp2", &unsupported_progression_manager),
          "Accepted spatial progression with unsupported component geometry");

    jpeg2000::FileManager multi_tile_manager;
    Check(!OpenImage(directory, "multi-tile.jp2", &multi_tile_manager),
          "Accepted a multi-tile codestream");
    jpeg2000::FileManager tile_index_manager;
    Check(!OpenImage(directory, "bad-tile-index.jp2", &tile_index_manager),
          "Accepted a nonzero tile index");
    jpeg2000::FileManager maximum_tile_parts_manager;
    Check(OpenImage(directory, "64-tile-parts.jp2", &maximum_tile_parts_manager),
          "Rejected the maximum supported tile-part count");
    data::File *maximum_tile_parts_file =
            maximum_tile_parts_manager.GetFile(directory + "64-tile-parts.jp2");
    Check(maximum_tile_parts_file != NULL &&
              maximum_tile_parts_manager.GetImage()->GetPacket(
                      maximum_tile_parts_file, 0,
                      jpeg2000::Packet(63, 0, 0, jpeg2000::Point()), &packet),
          "Could not index the maximum supported tile-part count");
    jpeg2000::FileManager excessive_tile_parts_manager;
    Check(!OpenImage(directory, "65-tile-parts.jp2", &excessive_tile_parts_manager),
          "Accepted too many tile-parts for the packet index");

    string large_file = directory + "large.jp2";
    WriteFile(large_file, jp2);
    Check(truncate(large_file.c_str(), static_cast<off_t>(UINT32_MAX) + 1) == 0,
          "Could not create a sparse large-file fixture");
    jpeg2000::FileManager large_file_manager;
    Check(!OpenImage(directory, "large.jp2", &large_file_manager),
          "Accepted a file larger than the packet index can address");

    jpeg2000::FileManager default_precinct_manager;
    Check(OpenImage(directory, "default-precincts.jp2", &default_precinct_manager),
          "Rejected a valid default precinct partition");
    data::File *default_precinct_file =
            default_precinct_manager.GetFile(directory + "default-precincts.jp2");
    Check(default_precinct_file != NULL &&
              default_precinct_manager.GetImage()->GetPacket(
                      default_precinct_file, 0,
                      jpeg2000::Packet(0, 0, 0, jpeg2000::Point(2, 0)), &packet),
          "Did not apply the default precinct size");
    jpeg2000::FileManager nonzero_origin_manager;
    Check(!OpenImage(directory, "nonzero-origin.jp2", &nonzero_origin_manager),
          "Accepted an unsupported nonzero image origin");

    jpeg2000::FileManager embedded_manager;
    Check(OpenImage(directory, "embedded.jpx", &embedded_manager),
          "Could not parse valid embedded JPX");
    Check(embedded_manager.GetImage()->GetNumCodestreams() == 1,
          "Wrong embedded JPX codestream count");

    jpeg2000::FileManager embedded_two_manager;
    Check(OpenImage(directory, "embedded-two.jpx", &embedded_two_manager),
          "Could not parse embedded JPX with distinct codestreams");
    Check(embedded_two_manager.GetImage()->GetNumCodestreams() == 2 &&
              embedded_two_manager.GetImage()->GetCodingParameters(0)->size.x == 1 &&
              embedded_two_manager.GetImage()->GetCodingParameters(1)->size.x == 2,
          "Did not retain embedded codestream parameters");
    data::File *embedded_file =
            embedded_two_manager.GetFile(directory + "embedded-two.jpx");
    Check(embedded_file != NULL &&
              embedded_two_manager.GetImage()->GetPacket(
                      embedded_file, 1,
                      jpeg2000::Packet(0, 0, 0, jpeg2000::Point()), &packet),
          "Could not index the second embedded codestream");

    jpip::Request multi_stream_request;
    Check(multi_stream_request.Parse(
              "GET /jpip?stream=0:1&fsiz=2,1&rsiz=2,1&roff=0,0&cid=0 HTTP/1.1"),
          "Could not parse a multi-codestream window request");
    jpip::DataBinServer multi_stream_server;
    Check(multi_stream_server.SetRequest(embedded_two_manager,
                                         multi_stream_request),
          "Rejected a heterogeneous multi-codestream window");
    char multi_stream_response[4096];
    int multi_stream_length = sizeof multi_stream_response;
    bool multi_stream_last = false;
    Check(multi_stream_server.GenerateChunk(embedded_two_manager,
                                             multi_stream_response,
                                             &multi_stream_length,
                                             &multi_stream_last) &&
              multi_stream_length > 3 && multi_stream_last,
          "Could not generate a heterogeneous multi-codestream response");

    jpip::Request second_stream_request;
    Check(second_stream_request.Parse(
              "GET /jpip?stream=1&fsiz=2,1&rsiz=2,1&roff=0,0&cid=0 HTTP/1.1") &&
              multi_stream_server.SetRequest(embedded_two_manager,
                                              second_stream_request),
          "Could not select the second codestream after a range response");
    multi_stream_length = sizeof multi_stream_response;
    Check(multi_stream_server.GenerateChunk(embedded_two_manager,
                                             multi_stream_response,
                                             &multi_stream_length,
                                             &multi_stream_last),
          "Could not generate a response for the cached second codestream");
    Check(multi_stream_length == 3 && multi_stream_last &&
              multi_stream_response[0] == 0 &&
              multi_stream_response[1] == jpip::EOR::WINDOW_DONE &&
              multi_stream_response[2] == 0,
          "The range response did not complete every selected codestream");

    jpeg2000::FileManager linked_manager;
    Check(OpenImage(directory, "linked.jpx", &linked_manager),
          "Could not parse valid linked JPX");
    Check(linked_manager.GetImage()->GetNumCodestreams() == 1,
          "Wrong linked JPX codestream count");

    vector<unsigned char> malformed_plt = jp2;
    size_t plt = 8 + codestream.size() - 11;
    malformed_plt[plt + 2] = 0;
    malformed_plt[plt + 3] = 2;
    WriteFile(directory + "bad-plt.jp2", malformed_plt);
    jpeg2000::FileManager malformed_plt_manager;
    Check(!OpenImage(directory, "bad-plt.jp2", &malformed_plt_manager),
          "Accepted invalid PLT marker length");

    vector<unsigned char> truncated = jp2;
    truncated.pop_back();
    WriteFile(directory + "truncated.jp2", truncated);
    jpeg2000::FileManager truncated_manager;
    Check(!OpenImage(directory, "truncated.jp2", &truncated_manager),
          "Accepted truncated JP2");

    WriteFile(directory + "truncated-linked.jpx",
              MakeLinkedJPX(directory + "truncated.jp2", codestream.size()));
    jpeg2000::FileManager truncated_linked_manager;
    Check(!OpenImage(directory, "truncated-linked.jpx", &truncated_linked_manager),
          "Accepted a JPX linked to a truncated JP2");

    WriteFile(directory + "bad-reference.jpx",
              MakeLinkedJPX(directory + "image.jp2", codestream.size(), 2));
    jpeg2000::FileManager reference_manager;
    Check(!OpenImage(directory, "bad-reference.jpx", &reference_manager),
          "Accepted inconsistent JPX data-reference count");

    WriteFile(directory + "bad-fragment.jpx",
              MakeLinkedJPX(directory + "image.jp2", codestream.size() - 1));
    jpeg2000::FileManager fragment_manager;
    Check(!OpenImage(directory, "bad-fragment.jpx", &fragment_manager),
          "Accepted invalid JPX fragment range");

    jpip::Request request;
    Check(request.Parse("GET /jpip?fsiz=1,1&rsiz=1,1&roff=0,0&cid=0 HTTP/1.1"),
          "Could not parse default-codestream request");
    jpip::DataBinServer server;
    Check(server.SetRequest(manager, request), "Rejected default codestream");
    char response[4096];
    int response_length = sizeof response;
    bool last = false;
    Check(server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate default-codestream response");
    Check(response_length > 0, "Generated empty default-codestream response");
    Check(last, "Did not complete an unlimited response");
    Check(response_length >= 3 && response[response_length - 3] == 0 &&
              response[response_length - 2] == jpip::EOR::WINDOW_DONE &&
              response[response_length - 1] == 0,
          "Unlimited response has no window-done EOR");

    jpip::Request default_window_request;
    Check(default_window_request.Parse("GET /jpip?fsiz=1,1&cid=0 HTTP/1.1"),
          "Could not parse a window with default region and offset");
    jpip::DataBinServer default_window_server;
    Check(default_window_server.SetRequest(manager, default_window_request),
          "Rejected a window with default region and offset");
    response_length = sizeof response;
    Check(default_window_server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate a response for a window with defaults");
    Check(response_length > 0 && last,
          "Did not complete a response for a window with defaults");

    jpip::Request short_request;
    Check(short_request.Parse("GET /jpip?len=2&cid=0 HTTP/1.1"),
          "Could not parse a response limit shorter than an EOR message");
    Check(server.SetRequest(manager, short_request), "Rejected a short response limit");
    response_length = sizeof response;
    Check(server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate a short limited response");
    Check(response_length == 0 && last,
          "Generated an incomplete JPIP message for a short response limit");

    jpip::DataBinServer limited_server;
    jpip::Request limited_request;
    Check(limited_request.Parse(
              "GET /jpip?fsiz=1,1&rsiz=1,1&roff=0,0&len=128&cid=0 HTTP/1.1"),
          "Could not parse a limited request");
    Check(limited_server.SetRequest(manager, limited_request),
          "Rejected a limited request");
    response_length = sizeof response;
    Check(limited_server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate a limited response");
    Check(last && response_length <= 128 && response_length >= 3 &&
              response[response_length - 3] == 0 &&
              response[response_length - 2] == jpip::EOR::BYTE_LIMIT_REACHED &&
              response[response_length - 1] == 0,
          "Limited response has no byte-limit EOR");

    jpip::DataBinServer model_server;
    jpip::Request model_request;
    Check(model_request.Parse("GET /jpip?model=M0,Hm,H0,P0&cid=0 HTTP/1.1") &&
              model_server.SetRequest(manager, model_request),
          "Rejected a valid cache model");
    Check(model_request.Parse("GET /jpip?model=M2147483647&cid=0 HTTP/1.1") &&
              !model_server.SetRequest(manager, model_request),
          "Accepted an unavailable metadata bin");
    Check(model_request.Parse("GET /jpip?model=[0-100000]Hm&cid=0 HTTP/1.1") &&
              model_request.model.size() == 1 &&
              !model_server.SetRequest(manager, model_request),
          "Accepted an unavailable cache-model codestream range");
    Check(model_request.Parse("GET /jpip?model=P2147483647&cid=0 HTTP/1.1") &&
              !model_server.SetRequest(manager, model_request),
          "Accepted an unavailable precinct bin");
    Check(model_request.Parse("GET /jpip?model=H1&cid=0 HTTP/1.1") &&
              !model_server.SetRequest(manager, model_request),
          "Accepted an unavailable tile-header bin");

    jpip::Request headers_only_request;
    Check(headers_only_request.Parse(
              "GET /jpip?fsiz=1,1&rsiz=1,1&roff=0,0&len=0&cid=0 HTTP/1.1"),
          "Could not parse a headers-only request");
    Check(server.SetRequest(manager, headers_only_request), "Rejected a headers-only request");
    response_length = sizeof response;
    Check(server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate a headers-only response");
    Check(response_length == 0 && last, "Generated data for a headers-only request");

    jpip::Request unlimited_request;
    Check(unlimited_request.Parse(
              "GET /jpip?fsiz=1,1&rsiz=1,1&roff=0,0&cid=0 HTTP/1.1"),
          "Could not parse a second unlimited request");
    Check(server.SetRequest(manager, unlimited_request), "Rejected a second unlimited request");
    response_length = sizeof response;
    Check(server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate a second unlimited response");
    Check(response_length == 3 && last && response[0] == 0 &&
              response[1] == jpip::EOR::WINDOW_DONE && response[2] == 0,
          "A previous response limit affected an unlimited response");

    jpip::Request cropped_request;
    Check(cropped_request.Parse(
              "GET /jpip?fsiz=4096,4096&rsiz=2000000000,2000000000&"
              "roff=-5,-5&len=512&cid=0 HTTP/1.1"),
          "Could not parse a partially overlapping window");
    jpip::DataBinServer cropped_server;
    Check(cropped_server.SetRequest(manager, cropped_request),
          "Rejected a partially overlapping window");
    response_length = sizeof response;
    Check(cropped_server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate a cropped-window response");

    jpip::Request outside_request;
    Check(outside_request.Parse(
              "GET /jpip?fsiz=4096,4096&rsiz=2000000000,2000000000&"
              "roff=2000000000,2000000000&len=512&cid=0 HTTP/1.1"),
          "Could not parse an out-of-range window");
    jpip::DataBinServer outside_server;
    Check(outside_server.SetRequest(manager, outside_request),
          "Rejected a window outside the image");
    response_length = sizeof response;
    Check(outside_server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate an empty-window response");
    Check(response_length == 3 && last && response[0] == 0 &&
              response[1] == jpip::EOR::WINDOW_DONE && response[2] == 0,
          "An outside window did not produce only a window-done EOR");

    jpip::Request empty_request;
    Check(empty_request.Parse(
              "GET /jpip?fsiz=4096,4096&rsiz=0,1&roff=0,0&len=512&cid=0 HTTP/1.1"),
          "Could not parse an empty window");
    jpip::DataBinServer empty_server;
    Check(empty_server.SetRequest(manager, empty_request), "Rejected an empty window");
    response_length = sizeof response;
    Check(empty_server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate a zero-sized-window response");
    Check(response_length == 3 && last && response[0] == 0 &&
              response[1] == jpip::EOR::WINDOW_DONE && response[2] == 0,
          "A zero-sized window did not produce only a window-done EOR");

    Check(request.Parse("GET /jpip?stream=1&len=512&cid=0 HTTP/1.1"),
          "Could not parse unavailable-codestream request");
    Check(!server.SetRequest(manager, request), "Accepted unavailable codestream");

    remove((directory + "image.jp2").c_str());
    remove((directory + "pcrl.jp2").c_str());
    remove((directory + "cprl.jp2").c_str());
    remove((directory + "unsupported-pcrl.jp2").c_str());
    remove((directory + "multi-tile.jp2").c_str());
    remove((directory + "bad-tile-index.jp2").c_str());
    remove((directory + "64-tile-parts.jp2").c_str());
    remove((directory + "65-tile-parts.jp2").c_str());
    remove(large_file.c_str());
    remove((directory + "default-precincts.jp2").c_str());
    remove((directory + "nonzero-origin.jp2").c_str());
    manager.ClearFiles();
    Check(request.Parse("GET /jpip?stream=0&len=512&cid=0 HTTP/1.1"),
          "Could not parse missing-file request");
    Check(server.SetRequest(manager, request), "Rejected valid missing-file request state");
    response_length = sizeof response;
    Check(!server.GenerateChunk(manager, response, &response_length, &last),
          "Generated a response after its source file disappeared");

    remove((directory + "embedded.jpx").c_str());
    remove((directory + "embedded-two.jpx").c_str());
    remove((directory + "linked.jpx").c_str());
    remove((directory + "bad-plt.jp2").c_str());
    remove((directory + "truncated.jp2").c_str());
    remove((directory + "truncated-linked.jpx").c_str());
    remove((directory + "bad-reference.jpx").c_str());
    remove((directory + "bad-fragment.jpx").c_str());
    rmdir(directory_name);
    return EXIT_SUCCESS;
}
