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

static vector<unsigned char> MakeCodestream(uint8_t progression = 0, uint8_t sampling = 1) {
    vector<unsigned char> codestream;
    Append16(codestream, 0xFF4F); // SOC

    Append16(codestream, 0xFF51); // SIZ
    Append16(codestream, 41);
    Append16(codestream, 0);      // Rsiz
    Append32(codestream, 1);      // Xsiz
    Append32(codestream, 1);      // Ysiz
    Append32(codestream, 0);      // XOsiz
    Append32(codestream, 0);      // YOsiz
    Append32(codestream, 1);      // XTsiz
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
    Append16(codestream, 1);      // layers
    codestream.push_back(0);      // MCT
    codestream.push_back(0);      // decomposition levels
    codestream.push_back(0);      // code-block width
    codestream.push_back(0);      // code-block height
    codestream.push_back(0);      // code-block style
    codestream.push_back(1);      // reversible transform

    Append16(codestream, 0xFF5C); // QCD
    Append16(codestream, 3);
    codestream.push_back(0);

    Append16(codestream, 0xFF90); // SOT
    Append16(codestream, 10);
    Append16(codestream, 0);      // tile index
    Append32(codestream, 21);     // tile-part length
    codestream.push_back(0);      // tile-part index
    codestream.push_back(1);      // number of tile-parts

    Append16(codestream, 0xFF58); // PLT
    Append16(codestream, 4);
    codestream.push_back(0);      // Zplt
    codestream.push_back(1);      // one-byte packet
    Append16(codestream, 0xFF93); // SOD
    codestream.push_back(0);      // packet data
    Append16(codestream, 0xFFD9); // EOC
    return codestream;
}

static vector<unsigned char> MakeJP2(const vector<unsigned char> &codestream) {
    vector<unsigned char> file;
    AppendBox(file, 0x6A703263, codestream); // jp2c
    return file;
}

static vector<unsigned char> MakeEmbeddedJPX(const vector<unsigned char> &codestream) {
    vector<unsigned char> file;
    AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch
    AppendBox(file, 0x6A703263, codestream);              // jp2c
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
    ofstream out(path.c_str(), ios::binary);
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
    WriteFile(directory + "embedded.jpx", MakeEmbeddedJPX(codestream));
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

    jpeg2000::FileManager embedded_manager;
    Check(OpenImage(directory, "embedded.jpx", &embedded_manager),
          "Could not parse valid embedded JPX");
    Check(embedded_manager.GetImage()->GetNumCodestreams() == 1,
          "Wrong embedded JPX codestream count");

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
    Check(request.Parse("GET /jpip?fsiz=1,1&rsiz=1,1&roff=0,0&len=512&cid=0 HTTP/1.1"),
          "Could not parse default-codestream request");
    jpip::DataBinServer server;
    Check(server.SetRequest(manager, request), "Rejected default codestream");
    char response[512];
    int response_length = sizeof response;
    bool last = false;
    Check(server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate default-codestream response");
    Check(response_length > 0, "Generated empty default-codestream response");

    Check(request.Parse("GET /jpip?stream=1&len=512&cid=0 HTTP/1.1"),
          "Could not parse unavailable-codestream request");
    Check(!server.SetRequest(manager, request), "Accepted unavailable codestream");

    remove((directory + "image.jp2").c_str());
    remove((directory + "pcrl.jp2").c_str());
    remove((directory + "cprl.jp2").c_str());
    remove((directory + "unsupported-pcrl.jp2").c_str());
    manager.ClearFiles();
    Check(request.Parse("GET /jpip?stream=0&len=512&cid=0 HTTP/1.1"),
          "Could not parse missing-file request");
    Check(server.SetRequest(manager, request), "Rejected valid missing-file request state");
    response_length = sizeof response;
    Check(!server.GenerateChunk(manager, response, &response_length, &last),
          "Generated a response after its source file disappeared");

    remove((directory + "embedded.jpx").c_str());
    remove((directory + "linked.jpx").c_str());
    remove((directory + "bad-plt.jp2").c_str());
    remove((directory + "truncated.jp2").c_str());
    remove((directory + "bad-reference.jpx").c_str());
    remove((directory + "bad-fragment.jpx").c_str());
    rmdir(directory_name);
    return EXIT_SUCCESS;
}
