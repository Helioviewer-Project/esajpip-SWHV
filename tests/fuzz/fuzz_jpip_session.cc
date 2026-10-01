#include "jpip_support.h"
#include <climits>
#include <memory>
#include <string>
#include "jpip/index/image_index.h"
#include "jpip/request/request.h"
#include "jpip/response/databin_server.h"

namespace {
using namespace jpip_fuzz;

void box(Bytes &out, uint32_t type, const Bytes &body) {
    put(out, 8 + body.size(), 4); put(out, type, 4);
    out.insert(out.end(), body.begin(), body.end());
}

struct Synthetic {
    Bytes codestream, header;
    std::map<int, Bytes> precincts;
};

// Two resolutions/components/layers, a 3x2 precinct grid, and distinct packet
// bytes. This is a wire fixture, not output from the production format writer.
Synthetic codestream(unsigned progression, unsigned padding) {
    struct Packet { int l, r, c, x, y; };
    std::vector<Packet> packets;
    Synthetic result;
    for (int r = 0; r < 2; ++r) for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x) for (int c = 0; c < 2; ++c)
            for (int l = 0; l < 2; ++l) {
                packets.push_back({l, r, c, x, y});
                result.precincts[c + 2 * (6 * r + 3 * y + x)].push_back(
                        static_cast<uint8_t>(1 + l + 2 * c + 4 * r + 8 * y + 24 * x));
            }
    auto order = [progression](const Packet &p) {
        switch (progression) {
        case 0: return std::make_tuple(p.l, p.r, p.c, p.y, p.x);
        case 1: return std::make_tuple(p.r, p.l, p.c, p.y, p.x);
        case 2: return std::make_tuple(p.r, p.y, p.x, p.c, p.l);
        case 3: return std::make_tuple(p.y, p.x, p.c, p.r, p.l);
        default: return std::make_tuple(p.c, p.y, p.x, p.r, p.l);
        }
    };
    std::sort(packets.begin(), packets.end(), [&](const Packet &a, const Packet &b) { return order(a) < order(b); });
    Bytes &out = result.codestream;
    put(out, 0xff4f, 2); put(out, 0xff51, 2); put(out, 44, 2); put(out, 0, 2);
    for (unsigned value : {5u, 3u, 0u, 0u, 5u, 3u, 0u, 0u}) put(out, value, 4);
    put(out, 2, 2);
    for (int c = 0; c < 2; ++c) out.insert(out.end(), {7, 1, 1});
    put(out, 0xff52, 2); put(out, 14, 2); put(out, 1, 1); put(out, progression, 1); put(out, 2, 2);
    out.insert(out.end(), {0, 1, 0, 0, 0, 1, 0x00, 0x11});
    put(out, 0xff5c, 2); put(out, 7, 2); out.insert(out.end(), {0, 0, 0, 0, 0});
    result.header = out;
    for (int part = 0; part < 2; ++part) {
        // Zero PLT padding follows the final logical packet only.
        unsigned part_padding = part == 1 ? padding : 0;
        put(out, 0xff90, 2); put(out, 10, 2); put(out, 0, 2); put(out, 19 + 48 + part_padding, 4);
        put(out, part, 1); put(out, 2, 1);
        put(out, 0xff58, 2); put(out, 27 + part_padding, 2); put(out, 0, 1);
        for (int i = 0; i < 24; ++i) put(out, 1, 1);
        for (unsigned i = 0; i < part_padding; ++i) put(out, 0, 1);
        put(out, 0xff93, 2);
        for (int i = part * 24; i < (part + 1) * 24; ++i) {
            const Packet &p = packets[i];
            put(out, 1 + p.l + 2 * p.c + 4 * p.r + 8 * p.y + 24 * p.x, 1);
        }
    }
    put(out, 0xffd9, 2);
    return result;
}

struct Sources : jpip::SourceProvider {
    Bytes bytes;
    jpip::Source source;
    bool missing = false;
    explicit Sources(const Bytes &_bytes) : bytes(_bytes), source(bytes.data(), bytes.size()) {}
    const jpip::Source *GetSource(const std::string &path) override {
        return missing || path != "./synthetic" ? NULL : &source;
    }
    void remap() {
        Bytes replacement = bytes;
        bytes.swap(replacement);
        source = jpip::Source(bytes.data(), bytes.size());
    }
};

int respond(jpip::DataBinServer &session, Sources &sources,
            const jpip::ResponseRequest &request, int capacity, Received &received) {
    Bytes bytes;
    bool last = false;
    for (int calls = 0; !last; ++calls) {
        require(calls < 4096, "response does not finish");
        Buffer buffer(capacity);
        int length = capacity;
        require(session.GenerateChunk(sources, buffer.data(), &length, &last), session.GetError().c_str());
        buffer.check(length);
        require(length > 0 || last, "response made no progress");
        bytes.insert(bytes.end(), buffer.wire_bytes(), buffer.wire_bytes() + length);
    }
    if (request.has.len)
        require(bytes.size() <= static_cast<size_t>(std::max(3, request.length_response)), "response exceeded byte budget");
    int reason = received.decode(bytes);
    require(reason == jpip::EOR::WINDOW_DONE || reason == jpip::EOR::BYTE_LIMIT_REACHED ||
            reason == jpip::EOR::RESPONSE_LIMIT_REACHED, "wrong EOR reason");
    if (!request.has.len) require(reason == jpip::EOR::WINDOW_DONE, "small fixture hit implicit response limit");
    return reason;
}

void prefixes(const Received &received, const std::map<Key, Bytes> &expected) {
    for (const auto &bin : received.bins) {
        auto found = expected.find(bin.first);
        require(found != expected.end() && bin.second.size() <= found->second.size() &&
                std::equal(bin.second.begin(), bin.second.end(), found->second.begin()),
                "bin identifier or bytes differ from independent fixture");
        auto complete = received.complete.find(bin.first);
        require(complete != received.complete.end() &&
                (!complete->second || bin.second == found->second), "premature final-bin flag");
    }
}

void advertise(jpip::ResponseRequest &request, const Received &received) {
    request.has.model = true;
    for (const auto &entry : received.bins) {
        bool complete = received.complete.at(entry.first);
        request.model.push_back({static_cast<jpip::DataBinClass>(std::get<0>(entry.first)), true,
            static_cast<int>(std::get<1>(entry.first)), static_cast<int>(std::get<1>(entry.first)),
            static_cast<int>(std::get<2>(entry.first)), complete ? INT_MAX : static_cast<int>(entry.second.size())});
    }
}
}

extern "C" int replay_jpip_session(const uint8_t *data, size_t size) {
    Input input(data, size);
    unsigned progression = input.byte() % 5;
    bool jpx = input.byte() & 1;
    unsigned padding = input.byte() % 8;
    std::vector<Synthetic> streams{codestream(progression, padding)};
    if (jpx) streams.push_back(codestream((progression + 1) % 5, padding));
    Bytes file, signature, ftyp;
    put(signature, 0x0d0a870a, 4); box(file, 0x6a502020, signature);
    uint32_t brand = jpx ? 0x6a707820 : 0x6a703220;
    put(ftyp, brand, 4); put(ftyp, 0, 4); put(ftyp, brand, 4); box(file, 0x66747970, ftyp);
    Bytes metadata = file;
    std::map<Key, Bytes> expected;
    for (size_t cs = 0; cs < streams.size(); ++cs) {
        if (jpx) { box(file, 0x6a706368, {}); box(metadata, 0x6a706368, {}); }
        Bytes header;
        put(header, 8 + streams[cs].codestream.size(), 4); put(header, 0x6a703263, 4);
        box(file, 0x6a703263, streams[cs].codestream);
        put(metadata, 52, 4); put(metadata, 0x70686c64, 4); put(metadata, 4, 4); put(metadata, 0, 8);
        metadata.insert(metadata.end(), header.begin(), header.end());
        put(metadata, 0, 8); put(metadata, 0, 8); put(metadata, cs, 8);
        expected[Key(jpip::MAIN_HEADER, cs, 0)] = streams[cs].header;
        expected[Key(jpip::TILE_HEADER, cs, 0)] = {};
        for (const auto &bin : streams[cs].precincts) expected[Key(jpip::PRECINCT, cs, bin.first)] = bin.second;
    }
    expected[Key(jpip::META_DATA, 0, 0)] = metadata;
    Sources small_sources(file), large_sources(file);
    jpip::ImageIndex small_image("./synthetic"), large_image("./synthetic");
    require(small_image.Open(*small_sources.GetSource("./synthetic"), small_sources, jpx) &&
            large_image.Open(*large_sources.GetSource("./synthetic"), large_sources, jpx), "synthetic index failed");
    std::unique_ptr<jpip::DataBinServer> small(new jpip::DataBinServer), large(new jpip::DataBinServer);
    Received small_bins, large_bins;
    bool needs_model = false;
    const int budgets[] = {0, 1, 2, 3, 4, 59, 60, 61, 62, 100, 160, 220, INT_MAX};
    const int capacities[] = {128, 129, 255, 256, 512, 1024};
    // Request histories include viewport changes, layers, selectors and truthful
    // model updates. Each byte-limited attempt is followed by window completion.
    for (int step = 0; input.more() && step < 12; ++step) {
        unsigned mode = input.byte(), selector = input.byte();
        unsigned width = input.byte() % 7 + 1, height = input.byte() % 5 + 1;
        if (mode & 128) width = 0;
        int x = static_cast<int>(input.byte() % 9) - 2, y = static_cast<int>(input.byte() % 7) - 2;
        unsigned w = input.byte() % 8, h = input.byte() % 6;
        unsigned layers = input.byte() % 4;
        int budget = budgets[input.byte() % 13], capacity = capacities[input.byte() % 6];
        const char *rounds[] = {"round-down", "round-up", "closest"};
        std::string query = "/jpip?cid=7&layers=" + std::to_string(layers);
        if (mode & 1)
            query += "&fsiz=" + std::to_string(width) + "," + std::to_string(height) + "," + rounds[(mode >> 1) % 3];
        if ((mode & 9) == 9)
            query += "&roff=" + std::to_string(x) + "," + std::to_string(y) +
                     "&rsiz=" + std::to_string(w) + "," + std::to_string(h);
        if (selector & 1) query += "&stream=" + std::to_string((selector >> 1) % 3) + "-";
        if (selector & 8) query += "&context=jpxl<0->";
        jpip::Request request;
        require(request.ParseTarget(query), "constructed request did not parse");
        if (mode & 16) {
            jpip::ResponseRequest::ModelUpdate bad{jpip::PRECINCT, true, 0, 0, 0, 1};
            switch ((mode >> 1) % 8) {
            case 0: bad.bin_class = jpip::META_DATA; bad.id = 2; break;
            case 1: bad.id = 24; break;
            case 2: bad.bin_class = jpip::MAIN_HEADER; bad.id = 1; break;
            case 3: bad.bin_class = jpip::TILE_DATA; break;
            case 4: bad.id = -1; break;
            case 5: bad.amount = -1; break;
            case 6: bad.first_codestream = 1; bad.last_codestream = 0; break;
            case 7: bad.first_codestream = bad.last_codestream = static_cast<int>(streams.size()); break;
            }
            request.has.model = true;
            request.model.push_back(bad);
        }
        if (mode & 32) {
            small.reset(new jpip::DataBinServer); large.reset(new jpip::DataBinServer);
            needs_model = true;
        }
        if (needs_model) advertise(request, small_bins);
        if (mode & 64) { small_sources.remap(); large_sources.remap(); }
        std::string small_error, large_error;
        bool accepted = small->SetRequest(small_image, request, &small_error);
        require(large->SetRequest(large_image, request, &large_error) == accepted && small_error == large_error,
                "request acceptance differs between chunk sizes");
        if ((mode & 16) || (mode & 129) == 129)
            require(!accepted && !small_error.empty(), "invalid typed model or window accepted");
        if (!accepted) continue;
        needs_model = false;
        request.has.len = true; request.length_response = budget;
        require(small->SetRequest(small_image, request), "byte-limited request rejected");
        respond(*small, small_sources, request, capacity, small_bins);
        prefixes(small_bins, expected);
        request.has.len = false;
        require(small->SetRequest(small_image, request) && large->SetRequest(large_image, request), "continuation rejected");
        respond(*small, small_sources, request, capacity, small_bins);
        respond(*large, large_sources, request, 8192, large_bins);
        prefixes(small_bins, expected); prefixes(large_bins, expected);
        require(small_bins.bins == large_bins.bins && small_bins.complete == large_bins.complete,
                "chunking or byte-limit continuation changed cached bins");
    }
    jpip::ResponseRequest full;
    full.has.fsiz = true; full.resolution_size = jpip::Size(5, 3);
    full.AddStream(0, streams.size() - 1);
    if (needs_model) advertise(full, small_bins);
    require(small->SetRequest(small_image, full) && large->SetRequest(large_image, full), "final full view rejected");
    respond(*small, small_sources, full, 128, small_bins);
    respond(*large, large_sources, full, 8192, large_bins);
    require(small_bins.bins == expected && large_bins.bins == expected, "full view differs from independently constructed bins");
    for (const auto &bin : expected)
        require(small_bins.complete[bin.first] && large_bins.complete[bin.first], "full view did not complete every bin");
    // Controlled acquisition failure and caller-buffer errors have defined outputs.
    for (int capacity : {0, 1, 2, 128}) {
        jpip::DataBinServer failed;
        require(failed.SetRequest(small_image, full), "failure request rejected");
        small_sources.missing = true;
        Buffer buffer(capacity); int length = capacity; bool last = true;
        require(!failed.GenerateChunk(small_sources, buffer.data(), &length, &last) &&
                length == 0 && !last && !failed.GetError().empty(), "failure left outputs or diagnostic unset");
        buffer.check(length);
    }
    return 0;
}

#ifndef ESAJPIP_FUZZ_REPLAY
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    return replay_jpip_session(data, size);
}
#endif
