#pragma once

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "hv_client.h"
#include "jpip/index/image_index.h"
#include "jpip/response/databin_server.h"

namespace client_fuzz {
using Bytes = std::vector<uint8_t>;

inline void require(bool condition, const char *message) {
    if (!condition) { std::cerr << message << '\n'; std::abort(); }
}

struct Sources : jpip::SourceProvider {
    struct Entry { std::vector<char> bytes; jpip::Source source; };
    std::map<std::string, Entry> files;
    const jpip::Source *GetSource(const std::string &path) override {
        auto found = files.find(path);
        if (found != files.end()) return &found->second.source;
        std::ifstream input(path, std::ios::binary);
        if (!input) return nullptr;
        Entry &entry = files[path];
        entry.bytes.assign(std::istreambuf_iterator<char>(input), {});
        entry.source = jpip::Source(entry.bytes.data(), entry.bytes.size());
        return &entry.source;
    }
};

inline Bytes response(jpip::DataBinServer &server, jpip::ImageIndex &image, Sources &sources,
                      const jpip::ResponseRequest &request) {
    std::string error;
    require(server.SetRequest(image, request, &error), "client fuzz seed request failed");
    Bytes bytes;
    bool last = false;
    while (!last) {
        char chunk[4096];
        int size = sizeof chunk;
        require(server.GenerateChunk(sources, chunk, &size, &last), "client fuzz seed generation failed");
        const uint8_t *payload = reinterpret_cast<const uint8_t *>(chunk);
        bytes.insert(bytes.end(), payload, payload + size);
    }
    return bytes;
}

struct Context {
    Sources sources;
    jpip::ImageIndex image;
    Bytes opening, restoration;
    explicit Context(const std::string &path, bool jpx) : image(path) {
        const jpip::Source *file = sources.GetSource(path);
        require(file && image.Open(*file, sources, jpx), "client fuzz fixture cannot open");
        jpip::DataBinServer server;
        jpip::ResponseRequest request;
        request.AddStream(0, image.GetNumCodestreams() - 1);
        request.layers = 0;
        opening = response(server, image, sources, request);
        jpip::DataBinServer replacement;
        jpip::ResponseRequest restore;
        restore.AddStream(image.GetNumCodestreams(), image.GetNumCodestreams());
        restore.layers = 0;
        restoration = response(replacement, image, sources, restore);
    }
};

inline Context &context(unsigned selector) {
    // Immutable fixtures are loaded once, not opened on every fuzz iteration.
    switch (selector % 3) {
        case 0: { static Context rgb(CLIENT_RGB, false); return rgb; }
        case 1: { static Context gray(CLIENT_GRAY, false); return gray; }
        default: { static Context movie(CLIENT_MOVIE, true); return movie; }
    }
}

struct Client {
    hv_client *value = hv_client_create();
    Client() { require(value != nullptr, "client allocation failed"); }
    ~Client() { hv_client_destroy(value); }
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
};
}
