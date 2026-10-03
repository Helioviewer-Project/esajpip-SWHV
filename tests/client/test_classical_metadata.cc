// Classical esajpip keeps frame associations inline in metadata bin 0.
// TODO: When JHV drops classical esajpip support, remove this file and the
// client_classical_metadata entry in tests/client/CMakeLists.txt.
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "hv_cache.h"
#include "hv_metadata.h"

using Bytes = std::vector<uint8_t>;

static void check(bool ok, const std::string &message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

static Bytes box(const char *type, const Bytes &contents) {
    Bytes out;
    for (int n = 4; n--;) out.push_back(static_cast<uint8_t>((contents.size() + 8) >> (8 * n)));
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), contents.begin(), contents.end());
    return out;
}

static Bytes operator+(Bytes a, const Bytes &b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

int main() {
    hv_cache cache;
    hv_cache_begin(&cache);
    // Number lists determine the frame, independently of physical order.
    // The first association wins; unnamed frames inherit the file XML.
    const Bytes associations[] = {
        box("nlst", {2, 0, 0, 1}) + box("xml ", {'b'}),
        box("nlst", {1, 0, 0, 0}) + box("xml ", {'a'}),
        box("nlst", {1, 0, 0, 1}) + box("xml ", {'c'}),
        box("nlst", {1, 0, 0, 0}) + box("xml ", {'c'}) +
        box("nlst", {1, 0, 0, 3}) + box("xml ", {'d'})
    };
    Bytes root = box("xml ", {'f'});
    for (const Bytes &association : associations)
        root = root + box("phld", {0, 0, 0, 4}) + box("asoc", association);
    hv_jpp_message message = {};
    message.bin_class = HV_BIN_META_DATA;
    message.length = root.size();
    message.data = root.data();
    message.last_byte = 1;
    check(hv_cache_apply(&cache, &message), "classical metadata fixture refused");

    hv_metadata metadata = {};
    char error[256] = "";
    check(hv_metadata_open(&cache, &metadata, error, sizeof error) == 0 && metadata.count == 4,
          error);
    const uint8_t expected[] = {'a', 'b', 'f', 'd'};
    for (size_t frame = 0; frame < 4; frame++) {
        const uint8_t *xml;
        size_t size;
        check(hv_metadata_xml(&metadata, frame, &xml, &size, error, sizeof error) == 0 &&
              size == 1 && xml[0] == expected[frame], "classical frame association differs");
    }
    hv_metadata_close(&metadata);
    hv_cache_release(&cache);
}
