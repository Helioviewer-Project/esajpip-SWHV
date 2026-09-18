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

static const uint64_t JP2_CODESTREAM_OFFSET = 40;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static void CheckProgressionMappings() {
    for (int progression = 0; progression <= 4; ++progression) {
        jpeg2000::CodingParameters parameters;
        parameters.size = jpeg2000::Size(19, 13);
        parameters.num_levels = 3;
        parameters.num_layers = 4;
        parameters.num_components = 3;
        parameters.progression = progression;
        parameters.resolutions.emplace_back(1, 2);
        parameters.resolutions.emplace_back(2, 1);
        parameters.resolutions.emplace_back(3, 2);
        parameters.resolutions.emplace_back(2, 3);
        Check(parameters.FillPrecinctCounts(),
              "Could not build progression-order test geometry");

        vector<bool> seen(parameters.GetNumPackets());
        int count = 0;
        for (int layer = 0; layer < parameters.num_layers; ++layer)
            for (int resolution = 0; resolution <= parameters.num_levels;
                 ++resolution)
                for (int component = 0;
                     component < parameters.num_components; ++component)
                    for (int y = 0;
                         y < parameters.resolutions[resolution].num_precincts.y;
                         ++y)
                        for (int x = 0;
                             x < parameters.resolutions[resolution].num_precincts.x;
                             ++x) {
                            int index = parameters.GetProgressionIndex(
                                    jpeg2000::Packet(
                                            layer, resolution, component,
                                            jpeg2000::Point(x, y)));
                            Check(index >= 0 &&
                                          index < parameters.GetNumPackets() &&
                                          !seen[index],
                                  "Progression order does not map packets uniquely");
                            seen[index] = true;
                            ++count;
                        }
        Check(count == parameters.GetNumPackets(),
              "Progression order does not cover every packet");
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

static uint32_t Read32(const vector<unsigned char> &data, size_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           data[offset + 3];
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

static vector<unsigned char> MakePreamble(uint32_t brand) {
    vector<unsigned char> file;
    vector<unsigned char> signature;
    Append32(signature, 0x0D0A870A);
    AppendBox(file, 0x6A502020, signature); // jP

    vector<unsigned char> file_type;
    Append32(file_type, brand);
    Append32(file_type, 0);                 // minor version
    Append32(file_type, brand);             // compatibility
    AppendBox(file, 0x66747970, file_type); // ftyp
    return file;
}

static vector<unsigned char> MakeCodestream(uint8_t progression = 0, uint8_t sampling = 1,
                                            uint32_t image_width = 1, uint32_t tile_width = 1,
                                            uint16_t tile_index = 0,
                                            uint16_t quality_layers = 1,
                                            uint8_t tile_parts = 1,
                                            uint8_t packets_per_tile_part = 1,
                                            uint8_t code_block_style = 0,
                                            const vector<unsigned char> &precinct_sizes =
                                                    vector<unsigned char>()) {
    uint8_t transform_levels = precinct_sizes.empty() ? 0 : precinct_sizes.size() - 1;
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
    Append16(codestream, 12 + precinct_sizes.size());
    codestream.push_back(precinct_sizes.empty() ? 0 : 1); // Scod
    codestream.push_back(progression);
    Append16(codestream, quality_layers);
    codestream.push_back(0);      // MCT
    codestream.push_back(transform_levels);
    codestream.push_back(0);      // code-block width
    codestream.push_back(0);      // code-block height
    codestream.push_back(code_block_style); // code-block style
    codestream.push_back(1);      // reversible transform
    codestream.insert(codestream.end(), precinct_sizes.begin(), precinct_sizes.end());

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

static vector<unsigned char> MakePrecinctCodestream(uint8_t lowest,
                                                     uint8_t higher) {
    return MakeCodestream(0, 1, 1, 1, 0, 1, 1, 2, 0,
                          vector<unsigned char>{lowest, higher});
}

static vector<unsigned char> SetMCTComponents(
        vector<unsigned char> codestream,
        const vector<unsigned char> &depths) {
    Check(!depths.empty(), "No components in MCT test fixture");
    size_t siz = 2;
    Check(codestream[siz] == 0xFF && codestream[siz + 1] == 0x51,
          "SIZ marker not found in MCT test fixture");
    codestream[siz + 2] = (38 + 3 * depths.size()) >> 8;
    codestream[siz + 3] = (38 + 3 * depths.size()) & 0xFF;
    codestream[siz + 38] = depths.size() >> 8;
    codestream[siz + 39] = depths.size() & 0xFF;
    codestream.erase(codestream.begin() + siz + 40,
                     codestream.begin() + siz + 43);
    vector<unsigned char> components;
    for (unsigned char depth : depths) {
        components.push_back(depth);
        components.push_back(1);
        components.push_back(1);
    }
    codestream.insert(codestream.begin() + siz + 40,
                      components.begin(), components.end());

    for (size_t i = siz + 40 + components.size();
         i + 9 <= codestream.size(); ++i) {
        if (codestream[i] == 0xFF && codestream[i + 1] == 0x52) {
            codestream[i + 8] = 1;
            return codestream;
        }
    }
    Check(false, "COD marker not found in MCT test fixture");
    return codestream;
}

static vector<unsigned char> DuplicateMarker(vector<unsigned char> codestream,
                                              uint16_t marker) {
    for (size_t i = 0; i + 4 <= codestream.size(); ++i) {
        if (codestream[i] != (marker >> 8) || codestream[i + 1] != (marker & 0xFF))
            continue;
        size_t length = (codestream[i + 2] << 8) | codestream[i + 3];
        Check(length >= 2 && length <= codestream.size() - i - 2,
              "Invalid marker in JPEG 2000 test fixture");
        vector<unsigned char> segment(codestream.begin() + i,
                                      codestream.begin() + i + length + 2);
        codestream.insert(codestream.begin() + i + length + 2,
                          segment.begin(), segment.end());
        return codestream;
    }
    Check(false, "Marker not found in JPEG 2000 test fixture");
    return codestream;
}

static vector<unsigned char> SetTilePartNumbers(vector<unsigned char> codestream,
                                                 size_t index, uint8_t part,
                                                 uint8_t count) {
    for (size_t i = 0; i + 12 <= codestream.size(); ++i) {
        if (codestream[i] != 0xFF || codestream[i + 1] != 0x90 ||
            codestream[i + 2] != 0 || codestream[i + 3] != 10)
            continue;
        if (index != 0) {
            --index;
            continue;
        }
        codestream[i + 10] = part;
        codestream[i + 11] = count;
        return codestream;
    }
    Check(false, "SOT marker not found in JPEG 2000 test fixture");
    return codestream;
}

static vector<unsigned char> InsertMarkerInTilePart(
        vector<unsigned char> codestream, uint16_t marker, size_t tile_part) {
    size_t marker_offset = 0;
    for (; marker_offset + 4 <= codestream.size(); ++marker_offset) {
        if (codestream[marker_offset] == (marker >> 8) &&
            codestream[marker_offset + 1] == (marker & 0xFF))
            break;
    }
    Check(marker_offset + 4 <= codestream.size(),
          "Marker not found in JPEG 2000 test fixture");
    size_t marker_length = (codestream[marker_offset + 2] << 8) |
                           codestream[marker_offset + 3];
    vector<unsigned char> segment(codestream.begin() + marker_offset,
                                  codestream.begin() + marker_offset +
                                      marker_length + 2);

    for (size_t i = 0; i + 12 <= codestream.size(); ++i) {
        if (codestream[i] != 0xFF || codestream[i + 1] != 0x90)
            continue;
        if (tile_part != 0) {
            --tile_part;
            continue;
        }
        uint32_t psot = (codestream[i + 6] << 24) |
                        (codestream[i + 7] << 16) |
                        (codestream[i + 8] << 8) | codestream[i + 9];
        Set32(codestream, i + 6, psot + segment.size());
        codestream.insert(codestream.begin() + i + 12,
                          segment.begin(), segment.end());
        return codestream;
    }
    Check(false, "Tile-part not found in JPEG 2000 test fixture");
    return codestream;
}

static vector<unsigned char> InsertBeforeFirstSOT(
        vector<unsigned char> codestream,
        const vector<unsigned char> &segment) {
    for (size_t i = 0; i + 1 < codestream.size(); ++i) {
        if (codestream[i] == 0xFF && codestream[i + 1] == 0x90) {
            codestream.insert(codestream.begin() + i,
                              segment.begin(), segment.end());
            return codestream;
        }
    }
    Check(false, "SOT marker not found in JPEG 2000 test fixture");
    return codestream;
}

static vector<unsigned char> ShortenFirstPLT(vector<unsigned char> codestream) {
    for (size_t i = 0; i + 7 <= codestream.size(); ++i) {
        if (codestream[i] == 0xFF && codestream[i + 1] == 0x58) {
            codestream[i + 6] = 0;
            return codestream;
        }
    }
    Check(false, "PLT marker not found in JPEG 2000 test fixture");
    return codestream;
}

static vector<unsigned char> ShortenFirstTilePartData(
        vector<unsigned char> codestream) {
    for (size_t i = 0; i + 12 <= codestream.size(); ++i) {
        if (codestream[i] != 0xFF || codestream[i + 1] != 0x90)
            continue;
        uint32_t length = Read32(codestream, i + 6);
        size_t end = i + length;
        Check(length > 0 && end + 1 < codestream.size() &&
                  codestream[end - 1] == 0 &&
                  codestream[end] == 0xFF && codestream[end + 1] == 0x90,
              "Unexpected first tile-part layout");
        codestream.erase(codestream.begin() + end - 1);
        Set32(codestream, i + 6, length - 1);
        return codestream;
    }
    Check(false, "Tile-part not found in JPEG 2000 test fixture");
    return codestream;
}

static vector<unsigned char> SplitFirstPLT(vector<unsigned char> codestream) {
    size_t sot = 0;
    for (size_t i = 0; i + 7 <= codestream.size(); ++i) {
        if (codestream[i] == 0xFF && codestream[i + 1] == 0x90)
            sot = i;
        if (codestream[i] != 0xFF || codestream[i + 1] != 0x58)
            continue;
        Check(codestream[i + 2] == 0 && codestream[i + 3] == 5,
              "Unexpected PLT marker in JPEG 2000 test fixture");
        codestream[i + 3] = 4;
        codestream.insert(codestream.begin() + i + 6,
                          {0xFF, 0x58, 0, 4, 1});
        uint32_t psot = (codestream[sot + 6] << 24) |
                        (codestream[sot + 7] << 16) |
                        (codestream[sot + 8] << 8) | codestream[sot + 9];
        Set32(codestream, sot + 6, psot + 5);
        return codestream;
    }
    Check(false, "PLT marker not found in JPEG 2000 test fixture");
    return codestream;
}

static vector<unsigned char> RepeatFirstPLTIndex(vector<unsigned char> codestream) {
    codestream = SplitFirstPLT(codestream);
    for (size_t i = 0, found = 0; i + 4 < codestream.size(); ++i) {
        if (codestream[i] != 0xFF || codestream[i + 1] != 0x58)
            continue;
        if (++found == 2) {
            codestream[i + 4] = 0;
            return codestream;
        }
    }
    Check(false, "Second PLT marker not found in JPEG 2000 test fixture");
    return codestream;
}

static vector<unsigned char> PadFirstPLT(vector<unsigned char> codestream,
                                         unsigned char padding) {
    size_t sot = 0;
    for (size_t i = 0; i + 7 <= codestream.size(); ++i) {
        if (codestream[i] == 0xFF && codestream[i + 1] == 0x90)
            sot = i;
        if (codestream[i] != 0xFF || codestream[i + 1] != 0x58)
            continue;
        uint16_t lplt = (codestream[i + 2] << 8) | codestream[i + 3];
        codestream.insert(codestream.begin() + i + 2 + lplt,
                          {padding, padding});
        codestream[i + 2] = (lplt + 2) >> 8;
        codestream[i + 3] = (lplt + 2) & 0xFF;
        uint32_t psot = (codestream[sot + 6] << 24) |
                        (codestream[sot + 7] << 16) |
                        (codestream[sot + 8] << 8) | codestream[sot + 9];
        Set32(codestream, sot + 6, psot + 2);
        return codestream;
    }
    Check(false, "PLT marker not found in padded PLT fixture");
    return codestream;
}

static vector<unsigned char> MakeJP2(const vector<unsigned char> &codestream) {
    vector<unsigned char> file = MakePreamble(0x6A703220); // jp2
    AppendBox(file, 0x6A703263, codestream); // jp2c
    return file;
}

static vector<unsigned char> MakeEmbeddedJPX(const vector<unsigned char> &codestream,
                                             const vector<unsigned char> &second = {}) {
    vector<unsigned char> file = MakePreamble(0x6A707820); // jpx
    AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch
    AppendBox(file, 0x6A703263, codestream);              // jp2c
    if (!second.empty()) {
        AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch
        AppendBox(file, 0x6A703263, second);                  // jp2c
    }
    return file;
}

static void AppendLinkedCodestream(vector<unsigned char> &file,
                                   const string &linked_path,
                                   uint32_t codestream_length,
                                   uint16_t reference_count = 1) {
    vector<unsigned char> fragment;
    Append16(fragment, 1);                 // one fragment
    Append64(fragment, JP2_CODESTREAM_OFFSET);
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
}

static vector<unsigned char> MakeLinkedJPX(const string &linked_path,
                                            uint32_t codestream_length,
                                            uint16_t reference_count = 1) {
    vector<unsigned char> file = MakePreamble(0x6A707820); // jpx
    AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch
    AppendLinkedCodestream(file, linked_path, codestream_length,
                           reference_count);
    return file;
}

static vector<unsigned char> MakeHeaderFirstEmbeddedJPX(
        const vector<unsigned char> &first,
        const vector<unsigned char> &second) {
    vector<unsigned char> file = MakePreamble(0x6A707820); // jpx
    AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch 0
    AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch 1
    AppendBox(file, 0x6A703263, first);                   // jp2c 0
    AppendBox(file, 0x6A703263, second);                  // jp2c 1
    return file;
}

static vector<unsigned char> MakeMixedJPX(
        const vector<unsigned char> &codestream, const string &linked_path) {
    vector<unsigned char> file = MakePreamble(0x6A707820); // jpx
    AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch 0
    AppendBox(file, 0x6A706368, vector<unsigned char>()); // jpch 1
    AppendBox(file, 0x6A703263, codestream);              // jp2c 0
    AppendLinkedCodestream(file, linked_path, codestream.size()); // ftbl 1
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
    return manager->OpenImage(name) ==
           jpeg2000::FileManager::OpenResult::OPENED;
}

static jpeg2000::FileManager::OpenResult OpenImageResult(
        const string &directory, const string &name) {
    jpeg2000::FileManager manager;
    Check(manager.Init(directory), "Could not initialize the file manager");
    return manager.OpenImage(name);
}

static bool RejectDataRequest(jpeg2000::FileManager &manager,
                              const string &line) {
    jpip::Request request;
    jpip::DataBinServer server;
    return request.Parse(line) &&
           !server.SetRequest(*manager.GetImage(), request);
}

int main() {
    CheckProgressionMappings();

    char directory_template[] = "/tmp/esajpip-jpeg2000-XXXXXX";
    char *directory_name = mkdtemp(directory_template);
    Check(directory_name != NULL, "Could not create JPEG 2000 test directory");
    string directory = string(directory_name) + "/";

    vector<unsigned char> codestream = MakeCodestream();
    vector<unsigned char> jp2 = MakeJP2(codestream);
    WriteFile(directory + "image.jp2", jp2);
    WriteFile(directory + "wrong-brand.jp2",
              MakeEmbeddedJPX(codestream));
    vector<unsigned char> nested_codestream = MakePreamble(0x6A707820);
    vector<unsigned char> codestream_box;
    AppendBox(codestream_box, 0x6A703263, codestream);
    AppendBox(nested_codestream, 0x6A706368, codestream_box);
    WriteFile(directory + "nested-codestream.jpx", nested_codestream);
    Check(mkdir((directory + "nested").c_str(), 0700) == 0,
          "Could not create nested JPEG 2000 test directory");
    WriteFile(directory + "nested/relative.jpx",
              MakeLinkedJPX("../image.jp2", codestream.size()));
    WriteFile(directory + "empty-reference.jpx",
              MakeLinkedJPX("", codestream.size()));
    vector<unsigned char> marker_after_tile_part = codestream;
    marker_after_tile_part.insert(marker_after_tile_part.end() - 2,
                                  {0xFF, 0x5C, 0x00, 0x03, 0x00});
    WriteFile(directory + "marker-after-tile-part.jp2",
              MakeJP2(marker_after_tile_part));
    WriteFile(directory + "duplicate-cod.jp2",
              MakeJP2(DuplicateMarker(codestream, 0xFF52)));
    WriteFile(directory + "duplicate-qcd.jp2",
              MakeJP2(DuplicateMarker(codestream, 0xFF5C)));
    WriteFile(directory + "main-coc.jp2",
              MakeJP2(InsertBeforeFirstSOT(
                  codestream,
                  {0xFF, 0x53, 0, 9, 0, 0, 0, 0, 0, 0, 1})));
    WriteFile(directory + "main-poc.jp2",
              MakeJP2(InsertBeforeFirstSOT(
                  codestream,
                  {0xFF, 0x5F, 0, 9, 0, 0, 0, 1, 1, 0, 0})));
    WriteFile(directory + "wrong-tile-part-count.jp2",
              MakeJP2(SetTilePartNumbers(codestream, 0, 0, 2)));
    WriteFile(directory + "wrong-first-tile-part.jp2",
              MakeJP2(SetTilePartNumbers(codestream, 0, 1, 0)));
    WriteFile(directory + "inconsistent-tile-part-count.jp2",
              MakeJP2(SetTilePartNumbers(
                  MakeCodestream(0, 1, 1, 1, 0, 2, 2), 1, 1, 3)));
    for (uint16_t marker : {0xFF52, 0xFF5C}) {
        string name = marker == 0xFF52 ? "late-cod.jp2" : "late-qcd.jp2";
        WriteFile(directory + name,
                  MakeJP2(InsertMarkerInTilePart(
                      MakeCodestream(0, 1, 1, 1, 0, 2, 2), marker, 1)));
        name = marker == 0xFF52 ? "tile-cod.jp2" : "tile-qcd.jp2";
        WriteFile(directory + name,
                  MakeJP2(InsertMarkerInTilePart(MakeCodestream(), marker, 0)));
    }
    WriteFile(directory + "tile-com.jp2",
              MakeJP2(InsertMarkerInTilePart(
                  InsertBeforeFirstSOT(codestream,
                                       {0xFF, 0x64, 0, 4, 0, 1}),
                  0xFF64, 0)));
    WriteFile(directory + "short-plt.jp2",
              MakeJP2(ShortenFirstPLT(
                  MakeCodestream(0, 1, 1, 1, 0, 4, 2, 2))));
    WriteFile(directory + "extra-tile-part-packet.jp2",
              MakeJP2(MakeCodestream(0, 1, 1, 1, 0, 1, 2)));
    WriteFile(directory + "extra-plt-entry.jp2",
              MakeJP2(ShortenFirstTilePartData(
                      MakeCodestream(0, 1, 1, 1, 0, 4, 2, 2))));
    WriteFile(directory + "multiple-plt.jp2",
              MakeJP2(SplitFirstPLT(
                  MakeCodestream(0, 1, 1, 1, 0, 2, 1, 2))));
    WriteFile(directory + "repeated-plt-index.jp2",
              MakeJP2(RepeatFirstPLTIndex(
                  MakeCodestream(0, 1, 1, 1, 0, 2, 1, 2))));
    WriteFile(directory + "zero-padded-plt.jp2",
              MakeJP2(PadFirstPLT(MakeCodestream(), 0)));
    WriteFile(directory + "nonzero-padded-plt.jp2",
              MakeJP2(PadFirstPLT(MakeCodestream(), 1)));
    WriteFile(directory + "valid-mct.jp2",
              MakeJP2(SetMCTComponents(
                  MakeCodestream(0, 1, 1, 1, 0, 1, 1, 3),
                  {7, 7, 7})));
    WriteFile(directory + "short-mct.jp2",
              MakeJP2(SetMCTComponents(MakeCodestream(), {7})));
    WriteFile(directory + "mismatched-mct.jp2",
              MakeJP2(SetMCTComponents(
                  MakeCodestream(0, 1, 1, 1, 0, 1, 1, 3),
                  {7, 8, 7})));
    WriteFile(directory + "missing-signature.jp2",
              vector<unsigned char>(jp2.begin() + 12, jp2.end()));
    vector<unsigned char> bad_signature = jp2;
    Set32(bad_signature, 8, 0);
    WriteFile(directory + "bad-signature.jp2", bad_signature);
    vector<unsigned char> missing_file_type = jp2;
    Set32(missing_file_type, 16, 0x66726565); // free
    WriteFile(directory + "missing-file-type.jp2", missing_file_type);
    WriteFile(directory + "pcrl.jp2", MakeJP2(MakeCodestream(3)));
    WriteFile(directory + "cprl.jp2", MakeJP2(MakeCodestream(4)));
    for (int progression = 0; progression <= 4; ++progression) {
        WriteFile(directory + "progression-" + to_string(progression) + ".jp2",
                  MakeJP2(MakeCodestream(progression, 1, 4, 4, 0, 2, 1, 8, 0,
                                         vector<unsigned char>{0x00, 0x11})));
    }
    for (int progression = 0; progression <= 4; ++progression) {
        WriteFile(directory + "subsampled-" + to_string(progression) + ".jp2",
                  MakeJP2(MakeCodestream(progression, 2)));
    }
    WriteFile(directory + "multi-tile.jp2", MakeJP2(MakeCodestream(0, 1, 2, 1)));
    WriteFile(directory + "bad-tile-index.jp2", MakeJP2(MakeCodestream(0, 1, 1, 1, 1)));
    WriteFile(directory + "64-tile-parts.jp2",
              MakeJP2(MakeCodestream(0, 1, 1, 1, 0, 64, 64)));
    WriteFile(directory + "65-tile-parts.jp2",
              MakeJP2(MakeCodestream(0, 1, 1, 1, 0, 65, 65)));
    WriteFile(directory + "default-precincts.jp2",
              MakeJP2(MakeCodestream(0, 1, 65537, 65537, 0, 1, 1, 3)));
    vector<unsigned char> excessive_precincts =
            MakeCodestream(0, 1, INT_MAX, INT_MAX, 0, 1, 1, 1, 0,
                           vector<unsigned char>{0x00});
    Set32(excessive_precincts, 12, INT_MAX); // Ysiz
    Set32(excessive_precincts, 28, INT_MAX); // YTsiz
    WriteFile(directory + "excessive-precincts.jp2",
              MakeJP2(excessive_precincts));
    WriteFile(directory + "excessive-packets.jp2",
              MakeJP2(MakeCodestream(0, 1, INT_MAX, INT_MAX, 0, 2, 1, 1, 0,
                                      vector<unsigned char>{0x00})));
    WriteFile(directory + "code-block-style-63.jp2",
              MakeJP2(MakeCodestream(0, 1, 1, 1, 0, 1, 1, 1, 63)));
    WriteFile(directory + "code-block-style-64.jp2",
              MakeJP2(MakeCodestream(0, 1, 1, 1, 0, 1, 1, 1, 64)));
    WriteFile(directory + "code-block-style-255.jp2",
              MakeJP2(MakeCodestream(0, 1, 1, 1, 0, 1, 1, 1, 255)));
    WriteFile(directory + "precinct-lowest-zero.jp2",
              MakeJP2(MakePrecinctCodestream(0x00, 0x11)));
    WriteFile(directory + "precinct-higher-ppx-zero.jp2",
              MakeJP2(MakePrecinctCodestream(0x11, 0x10)));
    WriteFile(directory + "precinct-higher-ppy-zero.jp2",
              MakeJP2(MakePrecinctCodestream(0x11, 0x01)));
    vector<unsigned char> nonzero_origin = MakeCodestream(0, 1, 2, 2);
    Set32(nonzero_origin, 16, 1); // XOsiz
    WriteFile(directory + "nonzero-origin.jp2", MakeJP2(nonzero_origin));
    WriteFile(directory + "embedded.jpx", MakeEmbeddedJPX(codestream));
    WriteFile(directory + "embedded-two.jpx",
              MakeEmbeddedJPX(codestream,
                              MakeCodestream(0, 1, 2, 2, 0, 2, 1, 2)));
    WriteFile(directory + "header-first-embedded.jpx",
              MakeHeaderFirstEmbeddedJPX(
                      codestream,
                      MakeCodestream(0, 1, 2, 2, 0, 2, 1, 2)));
    WriteFile(directory + "mixed.jpx",
              MakeMixedJPX(codestream, directory + "image.jp2"));
    WriteFile(directory + "linked.jpx",
              MakeLinkedJPX(directory + "image.jp2", codestream.size()));
    string self_linked_file = directory + "self-linked.jpx";
    WriteFile(self_linked_file,
              MakeLinkedJPX(self_linked_file, codestream.size()));
    string outside_file = string(directory_name) + ".jp2";
    WriteFile(outside_file, jp2);
    WriteFile(directory + "outside-linked.jpx",
              MakeLinkedJPX(outside_file, codestream.size()));

    jpeg2000::FileManager manager;
    Check(OpenImage(directory, "image.jp2", &manager), "Could not parse valid JP2");
    Check(manager.GetImage()->GetNumCodestreams() == 1, "Wrong JP2 codestream count");

    jpeg2000::FileManager relative_link_manager;
    Check(OpenImage(directory, "nested/relative.jpx", &relative_link_manager),
          "Could not resolve a linked JPX URL relative to its containing file");
    Check(relative_link_manager.GetImage()->GetNumCodestreams() == 1,
          "Wrong relative linked-JPX codestream count");

    jpeg2000::FileManager marker_after_tile_part_manager;
    Check(!OpenImage(directory, "marker-after-tile-part.jp2",
                     &marker_after_tile_part_manager),
          "Accepted a marker segment after tile-part data");

    for (const char *name : {"duplicate-cod.jp2", "duplicate-qcd.jp2"}) {
        jpeg2000::FileManager duplicate_marker_manager;
        Check(!OpenImage(directory, name, &duplicate_marker_manager),
              "Accepted a repeated main-header COD or QCD marker");
    }

    for (const char *name : {"main-coc.jp2", "main-poc.jp2"}) {
        jpeg2000::FileManager main_override_manager;
        Check(!OpenImage(directory, name, &main_override_manager),
              "Accepted unsupported main-header coding instructions");
    }

    jpeg2000::FileManager valid_mct_manager;
    Check(OpenImage(directory, "valid-mct.jp2", &valid_mct_manager),
          "Rejected a valid Part 1 multiple component transform");
    for (const char *name : {"short-mct.jp2", "mismatched-mct.jp2"}) {
        jpeg2000::FileManager invalid_mct_manager;
        Check(!OpenImage(directory, name, &invalid_mct_manager),
              "Accepted invalid multiple component transform inputs");
    }

    for (const char *name : {"wrong-tile-part-count.jp2",
                             "wrong-first-tile-part.jp2",
                             "inconsistent-tile-part-count.jp2"}) {
        jpeg2000::FileManager tile_part_manager;
        Check(!OpenImage(directory, name, &tile_part_manager),
              "Accepted inconsistent JPEG 2000 tile-part numbering");
    }

    for (const char *name : {"tile-cod.jp2", "tile-qcd.jp2", "tile-com.jp2",
                             "late-cod.jp2", "late-qcd.jp2"}) {
        jpeg2000::FileManager tile_marker_manager;
        Check(!OpenImage(directory, name, &tile_marker_manager),
              "Accepted COD or QCD in a tile-part header");
    }

    for (const char *name : {"missing-signature.jp2", "bad-signature.jp2",
                             "missing-file-type.jp2", "wrong-brand.jp2"}) {
        jpeg2000::FileManager preamble_manager;
        Check(!OpenImage(directory, name, &preamble_manager),
              "Accepted an invalid JPEG 2000 file preamble");
    }

    jpeg2000::FileManager nested_codestream_manager;
    Check(!OpenImage(directory, "nested-codestream.jpx",
                     &nested_codestream_manager),
          "Accepted a JPX codestream box outside the top level");
    jpeg2000::FileManager empty_reference_manager;
    Check(!OpenImage(directory, "empty-reference.jpx",
                     &empty_reference_manager),
          "Accepted an empty JPX data-reference URL");

    data::File *file = manager.GetFile(directory + "image.jp2");
    Check(file != NULL, "Could not reopen valid JP2");
    data::FileSegment packet;
    Check(manager.GetImage()->GetPacket(file, 0,
                                        jpeg2000::Packet(0, 0, 0, jpeg2000::Point()),
                                        &packet),
          "Could not index valid JP2 packet");
    Check(packet.length == 1, "Wrong JP2 packet length");

    jpeg2000::FileManager repeated_plt_index_manager;
    Check(!OpenImage(directory, "repeated-plt-index.jp2",
                     &repeated_plt_index_manager),
          "Accepted duplicate PLT marker indices");

    jpeg2000::FileManager short_plt_manager;
    Check(OpenImage(directory, "short-plt.jp2", &short_plt_manager),
          "Rejected a lazy-indexed PLT coverage fixture during parsing");
    data::File *short_plt_file =
            short_plt_manager.GetFile(directory + "short-plt.jp2");
    Check(short_plt_file != NULL, "Could not open PLT coverage fixture");
    Check(short_plt_manager.GetImage()->GetPacket(
              short_plt_file, 0,
              jpeg2000::Packet(0, 0, 0, jpeg2000::Point()), &packet),
          "Could not index the first packet in the PLT coverage fixture");
    Check(!short_plt_manager.GetImage()->GetPacket(
              short_plt_file, 0,
              jpeg2000::Packet(1, 0, 0, jpeg2000::Point()), &packet),
          "Accepted PLT lengths shorter than their tile-part data");

    jpeg2000::FileManager extra_packet_manager;
    Check(OpenImage(directory, "extra-tile-part-packet.jp2",
                    &extra_packet_manager),
          "Rejected a lazy-indexed extra-packet fixture during parsing");
    data::File *extra_packet_file = extra_packet_manager.GetFile(
            directory + "extra-tile-part-packet.jp2");
    Check(extra_packet_file != NULL &&
              !extra_packet_manager.GetImage()->GetPacket(
                      extra_packet_file, 0,
                      jpeg2000::Packet(0, 0, 0, jpeg2000::Point()), &packet),
          "Accepted packet data beyond the COD-derived packet set");

    jpeg2000::FileManager extra_plt_manager;
    Check(OpenImage(directory, "extra-plt-entry.jp2", &extra_plt_manager),
          "Rejected a lazy-indexed extra-PLT fixture during parsing");
    data::File *extra_plt_file =
            extra_plt_manager.GetFile(directory + "extra-plt-entry.jp2");
    Check(extra_plt_file != NULL &&
              !extra_plt_manager.GetImage()->GetPacket(
                      extra_plt_file, 0,
                      jpeg2000::Packet(0, 0, 0, jpeg2000::Point()), &packet),
          "Accepted a PLT entry beyond the tile-part data");

    jpeg2000::FileManager multiple_plt_manager;
    Check(OpenImage(directory, "multiple-plt.jp2", &multiple_plt_manager),
          "Rejected multiple PLT markers in one tile-part");
    data::File *multiple_plt_file =
            multiple_plt_manager.GetFile(directory + "multiple-plt.jp2");
    Check(multiple_plt_file != NULL &&
              multiple_plt_manager.GetImage()->GetPacket(
                      multiple_plt_file, 0,
                      jpeg2000::Packet(1, 0, 0, jpeg2000::Point()), &packet),
          "Could not index packets across PLT markers");

    jpeg2000::FileManager zero_padding_manager;
    Check(OpenImage(directory, "zero-padded-plt.jp2", &zero_padding_manager),
          "Rejected zero-padded PLT fixture during parsing");
    data::File *zero_padding_file =
            zero_padding_manager.GetFile(directory + "zero-padded-plt.jp2");
    Check(zero_padding_file != NULL, "Could not open zero-padded PLT fixture");
    Check(zero_padding_manager.GetImage()->GetPacket(
                  zero_padding_file, 0,
                  jpeg2000::Packet(0, 0, 0, jpeg2000::Point()), &packet),
          "Rejected deployed zero PLT padding");
    Check(packet.length == 1, "Wrong packet before zero PLT padding");


    jpeg2000::FileManager nonzero_padding_manager;
    Check(OpenImage(directory, "nonzero-padded-plt.jp2",
                    &nonzero_padding_manager),
          "Rejected lazy-indexed nonzero PLT padding fixture during parsing");
    data::File *nonzero_padding_file = nonzero_padding_manager.GetFile(
            directory + "nonzero-padded-plt.jp2");
    Check(nonzero_padding_file != NULL,
          "Could not open nonzero PLT padding fixture");
    Check(!nonzero_padding_manager.GetImage()->GetPacket(
                  nonzero_padding_file, 0,
                  jpeg2000::Packet(0, 0, 0, jpeg2000::Point()), &packet),
          "Accepted nonzero data after the logical PLT packet list");

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

    for (int progression = 0; progression <= 4; ++progression) {
        string name = "progression-" + to_string(progression) + ".jp2";
        jpeg2000::FileManager progression_manager;
        Check(OpenImage(directory, name, &progression_manager),
              "Could not parse progression-order indexing fixture");
        data::File *progression_file = progression_manager.GetFile(directory + name);
        Check(progression_file != NULL,
              "Could not reopen progression-order indexing fixture");
        jpeg2000::ImageIndex *image = progression_manager.GetImage();
        const jpeg2000::CodingParameters *parameters =
                image->GetCodingParameters(0);

        jpeg2000::Packet first_packet(0, 0, 0, jpeg2000::Point());
        data::FileSegment first_segment;
        Check(image->GetPacket(progression_file, 0, first_packet, &first_segment),
              "Could not index the first packet");
        uint64_t first_plt_offset = progression_file->GetOffset();

        jpeg2000::Packet later_packet(0, 1, 0, jpeg2000::Point(1, 0));
        int later_index = parameters->GetProgressionIndex(later_packet);
        Check(later_index > 0 &&
                      image->GetPacket(progression_file, 0, later_packet, &packet),
              "Could not index a later packet");
        Check(progression_file->GetOffset() == first_plt_offset + later_index,
              "Indexed packets beyond the requested packet");
        Check(packet.offset == first_segment.offset + later_index &&
                      packet.length == 1,
              "Returned the wrong progression-order packet");

        jpeg2000::Packet earlier_packet(0, 0, 0, jpeg2000::Point(1, 0));
        int earlier_index = parameters->GetProgressionIndex(earlier_packet);
        uint64_t plt_offset = progression_file->GetOffset();
        Check(earlier_index < later_index &&
                      image->GetPacket(progression_file, 0, earlier_packet, &packet),
              "Could not retrieve an earlier packet after a later packet");
        Check(progression_file->GetOffset() == plt_offset &&
                      packet.offset == first_segment.offset + earlier_index,
              "Rebuilt or returned the wrong earlier packet");
    }
    for (int progression = 0; progression <= 4; ++progression) {
        jpeg2000::FileManager subsampled_manager;
        Check(!OpenImage(directory,
                         "subsampled-" + to_string(progression) + ".jp2",
                         &subsampled_manager),
              "Accepted component sampling unsupported by the packet index");
    }

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
    Check(truncate(large_file.c_str(), static_cast<off_t>(INT_MAX) + 1) == 0,
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
    for (const char *name : {"excessive-precincts.jp2",
                             "excessive-packets.jp2"}) {
        jpeg2000::FileManager excessive_packets_manager;
        Check(!OpenImage(directory, name, &excessive_packets_manager),
              "Accepted a packet count exceeding the index range");
    }
    jpeg2000::FileManager nonzero_origin_manager;
    Check(!OpenImage(directory, "nonzero-origin.jp2", &nonzero_origin_manager),
          "Accepted an unsupported nonzero image origin");

    jpeg2000::FileManager maximum_code_block_style_manager;
    Check(OpenImage(directory, "code-block-style-63.jp2",
                    &maximum_code_block_style_manager),
          "Rejected the maximum Part 1 code-block style");
    for (const char *name : {"code-block-style-64.jp2",
                             "code-block-style-255.jp2"}) {
        jpeg2000::FileManager reserved_code_block_style_manager;
        Check(!OpenImage(directory, name, &reserved_code_block_style_manager),
              "Accepted a reserved code-block style bit");
    }

    jpeg2000::FileManager lowest_zero_precinct_manager;
    Check(OpenImage(directory, "precinct-lowest-zero.jp2",
                    &lowest_zero_precinct_manager),
          "Rejected zero precinct exponents at the lowest resolution");
    for (const char *name : {"precinct-higher-ppx-zero.jp2",
                             "precinct-higher-ppy-zero.jp2"}) {
        jpeg2000::FileManager zero_precinct_manager;
        Check(!OpenImage(directory, name, &zero_precinct_manager),
              "Accepted a zero precinct exponent above the lowest resolution");
    }

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

    jpeg2000::FileManager header_first_manager;
    Check(OpenImage(directory, "header-first-embedded.jpx",
                    &header_first_manager),
          "Could not parse JPX with headers before its codestream boxes");
    const jpeg2000::Metadata &header_first_metadata =
            header_first_manager.GetImage()->GetMetadata();
    Check(header_first_metadata.bin0.size() == 2 &&
              header_first_metadata.bin0[0].placeholder.id == 0 &&
              header_first_metadata.bin0[1].placeholder.id == 1,
          "JPX placeholders do not follow physical codestream order");

    jpeg2000::FileManager mixed_manager;
    Check(!OpenImage(directory, "mixed.jpx", &mixed_manager),
          "Accepted mixed embedded and linked JPX codestreams");

    jpip::Request multi_stream_request;
    Check(multi_stream_request.Parse(
              "GET /jpip?stream=0-1&fsiz=2,1&rsiz=2,1&roff=0,0&cid=0 HTTP/1.1"),
          "Could not parse a multi-codestream window request");
    jpip::DataBinServer multi_stream_server;
    Check(multi_stream_server.SetRequest(*embedded_two_manager.GetImage(),
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
              multi_stream_server.SetRequest(*embedded_two_manager.GetImage(),
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

    jpip::Request unqualified_model_request;
    Check(unqualified_model_request.Parse(
              "GET /jpip?model=M0,Hm,H0,P0&stream=1&cid=0 HTTP/1.1"),
          "Could not parse an unqualified cache model");
    jpip::DataBinServer unqualified_model_server;
    Check(unqualified_model_server.SetRequest(*embedded_two_manager.GetImage(),
                                               unqualified_model_request),
          "Rejected an unqualified cache model for the selected codestream");
    multi_stream_length = sizeof multi_stream_response;
    Check(unqualified_model_server.GenerateChunk(embedded_two_manager,
                                                   multi_stream_response,
                                                   &multi_stream_length,
                                                   &multi_stream_last) &&
              multi_stream_length == 3 && multi_stream_last,
          "Applied an unqualified cache model to codestream 0");

    jpip::Request context_model_request;
    Check(context_model_request.Parse(
              "GET /jpip?context=jpxl%3C1%3E&model=M0,Hm,H0,P0&cid=0 HTTP/1.1"),
          "Could not parse an unqualified context cache model");
    jpip::DataBinServer context_model_server;
    Check(context_model_server.SetRequest(*embedded_two_manager.GetImage(),
                                           context_model_request),
          "Rejected an unqualified context cache model");
    multi_stream_length = sizeof multi_stream_response;
    Check(context_model_server.GenerateChunk(embedded_two_manager,
                                               multi_stream_response,
                                               &multi_stream_length,
                                               &multi_stream_last) &&
              multi_stream_length > 3 && multi_stream_last,
          "Applied an unqualified context cache model outside codestream 0");

    jpip::Request open_model_request;
    Check(open_model_request.Parse(
              "GET /jpip?model=[1-]Hm&cid=0 HTTP/1.1"),
          "Could not parse an open cache-model codestream range");
    jpip::DataBinServer open_model_server;
    Check(open_model_server.SetRequest(*embedded_two_manager.GetImage(),
                                       open_model_request),
          "Could not apply an open cache-model codestream range");

    jpip::DataBinServer default_stream_server;
    Check(default_stream_server.SetRequest(*embedded_two_manager.GetImage(),
                                           second_stream_request),
          "Could not establish a non-default codestream selection");
    multi_stream_length = sizeof multi_stream_response;
    Check(default_stream_server.GenerateChunk(embedded_two_manager,
                                                multi_stream_response,
                                                &multi_stream_length,
                                                &multi_stream_last) &&
              multi_stream_last,
          "Could not complete the non-default codestream response");
    jpip::Request default_stream_request;
    Check(default_stream_request.Parse(
              "GET /jpip?fsiz=1,1&rsiz=1,1&roff=0,0&cid=0 HTTP/1.1") &&
              default_stream_server.SetRequest(*embedded_two_manager.GetImage(),
                                                default_stream_request),
          "Could not restore the default codestream selection");
    multi_stream_length = sizeof multi_stream_response;
    Check(default_stream_server.GenerateChunk(embedded_two_manager,
                                                multi_stream_response,
                                                &multi_stream_length,
                                                &multi_stream_last) &&
              multi_stream_length > 3 && multi_stream_last,
          "An omitted stream retained the previous codestream selection");

    jpeg2000::FileManager linked_manager;
    Check(OpenImage(directory, "linked.jpx", &linked_manager),
          "Could not parse valid linked JPX");
    Check(linked_manager.GetImage()->GetNumCodestreams() == 1,
          "Wrong linked JPX codestream count");

    jpeg2000::FileManager self_linked_manager;
    Check(!OpenImage(directory, "self-linked.jpx", &self_linked_manager),
          "Accepted a JPX linked to itself");

    jpeg2000::FileManager outside_linked_manager;
    Check(OpenImage(directory, "outside-linked.jpx", &outside_linked_manager),
          "Rejected a trusted JPX link outside the image directory");

    string outside_name = outside_file.substr(outside_file.find_last_of('/') + 1);
    string target_traversal = "../" + outside_name;
    Check(OpenImageResult(directory, target_traversal) ==
                  jpeg2000::FileManager::OpenResult::INVALID_PATH,
          "Accepted parent traversal in a target path");
    string uri_traversal = "/../" + outside_name;
    Check(OpenImageResult(directory, uri_traversal) ==
                  jpeg2000::FileManager::OpenResult::INVALID_PATH,
          "Accepted parent traversal in a URI path");
    string embedded_traversal = "unused/../image.jp2";
    Check(OpenImageResult(directory, embedded_traversal) ==
                  jpeg2000::FileManager::OpenResult::INVALID_PATH,
          "Accepted an embedded parent path segment");
    string nul_path = "image.jp2";
    nul_path.push_back('\0');
    nul_path += ".jp2";
    Check(OpenImageResult(directory, nul_path) ==
                  jpeg2000::FileManager::OpenResult::INVALID_PATH,
          "Accepted a file path containing NUL");
    Check(OpenImageResult(directory, "image.jpeg") ==
                  jpeg2000::FileManager::OpenResult::UNSUPPORTED,
          "Did not distinguish an unsupported image type");
    Check(OpenImageResult(directory, "missing.jp2") ==
                  jpeg2000::FileManager::OpenResult::NOT_FOUND,
          "Did not distinguish a missing image");

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
    Check(server.SetRequest(*manager.GetImage(), request),
          "Rejected default codestream");
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
    Check(default_window_server.SetRequest(*manager.GetImage(),
                                           default_window_request),
          "Rejected a window with default region and offset");
    response_length = sizeof response;
    Check(default_window_server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate a response for a window with defaults");
    Check(response_length > 0 && last,
          "Did not complete a response for a window with defaults");

    for (int response_limit = 0; response_limit < jpip::DataBinWriter::EOR_LENGTH;
         response_limit++) {
        jpip::DataBinServer short_server;
        jpip::Request short_request;
        Check(short_request.Parse("GET /jpip?len=" + to_string(response_limit) +
                                  "&cid=0 HTTP/1.1"),
              "Could not parse a response limit shorter than an EOR message");
        Check(short_server.SetRequest(*manager.GetImage(), short_request),
              "Rejected a short response limit");
        response_length = sizeof response;
        Check(short_server.GenerateChunk(manager, response, &response_length, &last),
              "Could not generate a short limited response");
        Check(response_length == jpip::DataBinWriter::EOR_LENGTH && last &&
                  response[0] == 0 &&
                  response[1] == jpip::EOR::BYTE_LIMIT_REACHED &&
                  response[2] == 0,
              "Short limited response has no byte-limit EOR");
    }

    jpip::DataBinServer limited_server;
    jpip::Request limited_request;
    Check(limited_request.Parse(
              "GET /jpip?fsiz=1,1&rsiz=1,1&roff=0,0&len=128&cid=0 HTTP/1.1"),
          "Could not parse a limited request");
    Check(limited_server.SetRequest(*manager.GetImage(), limited_request),
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
              model_server.SetRequest(*manager.GetImage(), model_request),
          "Rejected a valid cache model");
    Check(RejectDataRequest(manager,
                           "GET /jpip?model=M2147483647&cid=0 HTTP/1.1"),
          "Accepted an unavailable metadata bin");
    Check(RejectDataRequest(manager,
                           "GET /jpip?model=[0-100000]Hm&cid=0 HTTP/1.1"),
          "Accepted an unavailable cache-model codestream range");
    Check(RejectDataRequest(manager,
                           "GET /jpip?model=P2147483647&cid=0 HTTP/1.1"),
          "Accepted an unavailable precinct bin");
    Check(RejectDataRequest(manager, "GET /jpip?model=H1&cid=0 HTTP/1.1"),
          "Accepted an unavailable tile-header bin");

    jpip::Request headers_only_request;
    Check(headers_only_request.Parse(
              "GET /jpip?fsiz=1,1&rsiz=1,1&roff=0,0&len=0&cid=0 HTTP/1.1"),
          "Could not parse a headers-only request");
    Check(server.SetRequest(*manager.GetImage(), headers_only_request),
          "Rejected a headers-only request");
    response_length = sizeof response;
    Check(server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate a headers-only response");
    Check(response_length == jpip::DataBinWriter::EOR_LENGTH && last &&
              response[0] == 0 && response[1] == jpip::EOR::WINDOW_DONE &&
              response[2] == 0,
          "Headers-only response has no window-done EOR");

    jpip::Request unlimited_request;
    Check(unlimited_request.Parse(
              "GET /jpip?fsiz=1,1&rsiz=1,1&roff=0,0&cid=0 HTTP/1.1"),
          "Could not parse a second unlimited request");
    Check(server.SetRequest(*manager.GetImage(), unlimited_request),
          "Rejected a second unlimited request");
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
    Check(cropped_server.SetRequest(*manager.GetImage(), cropped_request),
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
    Check(outside_server.SetRequest(*manager.GetImage(), outside_request),
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
    Check(empty_server.SetRequest(*manager.GetImage(), empty_request),
          "Rejected an empty window");
    response_length = sizeof response;
    Check(empty_server.GenerateChunk(manager, response, &response_length, &last),
          "Could not generate a zero-sized-window response");
    Check(response_length == 3 && last && response[0] == 0 &&
              response[1] == jpip::EOR::WINDOW_DONE && response[2] == 0,
          "A zero-sized window did not produce only a window-done EOR");

    jpip::Request unavailable_stream_request;
    Check(unavailable_stream_request.Parse(
              "GET /jpip?stream=1&fsiz=1,1&rsiz=1,1&roff=0,0&len=512&cid=0 HTTP/1.1"),
          "Could not parse unavailable codestream request");
    jpip::DataBinServer unavailable_stream_server;
    Check(unavailable_stream_server.SetRequest(*manager.GetImage(),
                                                unavailable_stream_request),
          "Rejected a non-existent codestream instead of ignoring it");
    response_length = sizeof response;
    Check(unavailable_stream_server.GenerateChunk(
                  manager, response, &response_length, &last) &&
              response_length == 3 && last && response[0] == 0 &&
              response[1] == jpip::EOR::WINDOW_DONE && response[2] == 0,
          "A non-existent codestream did not produce an empty response");

    jpip::Request missing_file_request;
    Check(missing_file_request.Parse("GET /jpip?stream=0&len=512&cid=0 HTTP/1.1"),
          "Could not parse missing-file request");
    jpip::DataBinServer missing_file_server;
    Check(missing_file_server.SetRequest(*manager.GetImage(),
                                        missing_file_request),
          "Rejected valid missing-file request state");
    manager.ClearFiles();

    remove((directory + "image.jp2").c_str());
    remove((directory + "wrong-brand.jp2").c_str());
    remove((directory + "nested-codestream.jpx").c_str());
    remove((directory + "empty-reference.jpx").c_str());
    remove((directory + "nested/relative.jpx").c_str());
    rmdir((directory + "nested").c_str());
    remove((directory + "pcrl.jp2").c_str());
    remove((directory + "cprl.jp2").c_str());
    for (int progression = 0; progression <= 4; ++progression)
        remove((directory + "subsampled-" + to_string(progression) + ".jp2").c_str());
    remove((directory + "multi-tile.jp2").c_str());
    remove((directory + "bad-tile-index.jp2").c_str());
    remove((directory + "64-tile-parts.jp2").c_str());
    remove((directory + "65-tile-parts.jp2").c_str());
    remove((directory + "late-cod.jp2").c_str());
    remove((directory + "late-qcd.jp2").c_str());
    remove((directory + "tile-cod.jp2").c_str());
    remove((directory + "tile-qcd.jp2").c_str());
    remove((directory + "tile-com.jp2").c_str());
    remove((directory + "main-coc.jp2").c_str());
    remove((directory + "main-poc.jp2").c_str());
    remove((directory + "short-plt.jp2").c_str());
    remove((directory + "extra-tile-part-packet.jp2").c_str());
    remove((directory + "extra-plt-entry.jp2").c_str());
    remove((directory + "multiple-plt.jp2").c_str());
    remove((directory + "repeated-plt-index.jp2").c_str());
    remove((directory + "zero-padded-plt.jp2").c_str());
    remove((directory + "nonzero-padded-plt.jp2").c_str());
    remove((directory + "valid-mct.jp2").c_str());
    remove((directory + "short-mct.jp2").c_str());
    remove((directory + "mismatched-mct.jp2").c_str());
    remove(large_file.c_str());
    remove((directory + "default-precincts.jp2").c_str());
    remove((directory + "nonzero-origin.jp2").c_str());
    response_length = sizeof response;
    Check(!missing_file_server.GenerateChunk(manager, response, &response_length, &last),
          "Generated a response after its source file disappeared");

    remove((directory + "embedded.jpx").c_str());
    remove((directory + "embedded-two.jpx").c_str());
    remove((directory + "header-first-embedded.jpx").c_str());
    remove((directory + "mixed.jpx").c_str());
    remove((directory + "linked.jpx").c_str());
    remove((directory + "outside-linked.jpx").c_str());
    remove(outside_file.c_str());
    remove((directory + "bad-plt.jp2").c_str());
    remove((directory + "truncated.jp2").c_str());
    remove((directory + "truncated-linked.jpx").c_str());
    remove((directory + "bad-reference.jpx").c_str());
    remove((directory + "bad-fragment.jpx").c_str());
    rmdir(directory_name);
    return EXIT_SUCCESS;
}
