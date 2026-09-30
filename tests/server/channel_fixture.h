#pragma once

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <zlib.h>

namespace channel_test {

inline void Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Fixture {
    std::string directory;
    Fixture() {
        char name[] = "/tmp/esajpip-channel-XXXXXX";
        Check(mkdtemp(name) != NULL, "Could not create channel fixture directory");
        directory = std::string(name) + "/";
        std::ifstream input(std::string(VECTORS) + "/jpx-graph-frame2.jp2",
                            std::ios::binary);
        Check(input.good(), "Could not read channel fixture");
        std::ofstream output(directory + "image.jp2", std::ios::binary);
        output << input.rdbuf();
        output.close();
        Check(output.good(), "Could not write channel fixture");
    }
    ~Fixture() {
        std::remove((directory + "image.jp2").c_str());
        std::remove((directory + "blocked.jp2").c_str());
        rmdir(directory.c_str());
    }
};

inline std::vector<char> Gunzip(const std::vector<char> &compressed) {
    z_stream stream = {};
    Check(inflateInit2(&stream, MAX_WBITS + 16) == Z_OK,
          "Could not initialize migrated gzip validation");
    stream.next_in = reinterpret_cast<Bytef *>(
            const_cast<char *>(compressed.data()));
    stream.avail_in = compressed.size();

    std::vector<char> result;
    char output[128];
    int status;
    do {
        stream.next_out = reinterpret_cast<Bytef *>(output);
        stream.avail_out = sizeof output;
        status = inflate(&stream, Z_NO_FLUSH);
        Check(status == Z_OK || status == Z_STREAM_END,
              "Could not decompress the migrated gzip response");
        result.insert(result.end(), output,
                      output + sizeof output - stream.avail_out);
    } while (status != Z_STREAM_END);
    Check(stream.avail_in == 0, "Gzip response contains trailing bytes");
    inflateEnd(&stream);
    return result;
}


}
