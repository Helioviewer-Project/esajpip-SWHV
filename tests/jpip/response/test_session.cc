#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <tuple>
#include <vector>

#include "jpip/index/image_index.h"
#include "jpip/response/databin_server.h"

using Bytes = std::vector<unsigned char>;
using Key = std::tuple<int, int, int>;

static void check(bool condition, const char *message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

struct MemorySources : jpip::SourceProvider {
    int released = 0;
    std::map<std::string, Bytes> bytes;
    std::map<std::string, jpip::Source> views;
    const jpip::Source *GetSource(const std::string &path) override {
        std::string key = path;
        while (key.compare(0,2,"./") == 0) key.erase(0,2);
        key.insert(0,"./");
        auto found = bytes.find(key);
        if (found == bytes.end()) return NULL;
        auto view = views.emplace(path, jpip::Source(found->second.data(), found->second.size()));
        return &view.first->second;
    }
    void ReleaseSource(const std::string &path) override { ++released; views.erase(path); }
    void load(const std::string &name) {
        std::ifstream input(std::string(VECTORS) + "/" + name, std::ios::binary);
        check(input.good(), "fixture missing");
        Bytes &contents = bytes["./" + name];
        contents.clear();
        for (std::istreambuf_iterator<char> i(input), end; i != end; ++i)
            contents.push_back(static_cast<unsigned char>(*i));
    }
    void remap() {
        // Copy before releasing to guarantee different backing addresses.
        std::map<std::string, Bytes> replacement = bytes;
        views.clear();
        bytes.swap(replacement);
    }
};

static uint64_t number(const Bytes &bytes, size_t &at, size_t n) {
    check(n <= bytes.size() - at, "reference read overrun");
    uint64_t result = 0;
    for (size_t i = 0; i < n; ++i) result = result * 256 + bytes[at++];
    return result;
}
static void put(Bytes &bytes, uint64_t value, size_t n) {
    while (n) { --n; bytes.push_back(static_cast<unsigned char>(value >> (n * 8))); }
}
static uint64_t vbas(const Bytes &bytes, size_t &at) {
    uint64_t result = 0;
    unsigned char byte;
    do {
        check(at < bytes.size(), "truncated JPP integer");
        byte = bytes[at++];
        check(result <= (UINT64_MAX >> 7), "JPP integer overflow");
        result = (result << 7) | (byte & 127);
    } while (byte & 128);
    return result;
}
struct Received {
    std::map<Key, Bytes> bins;
    std::map<Key, bool> complete;
    int messages = 0;
    int decode(const Bytes &bytes) {
        size_t at = 0;
        int cls = -1, stream = -1;
        while (at < bytes.size()) {
            unsigned char first = bytes[at++];
            if (first == 0) {
                check(at < bytes.size(), "missing EOR reason");
                int reason = bytes[at++];
                size_t n = vbas(bytes, at);
                check(n == bytes.size() - at, "unexpected data after EOR");
                return reason;
            }
            int present = (first >> 5) & 3;
            check(present != 0, "invalid JPP presence bits");
            uint64_t id = first & 15;
            if (first & 128) {
                unsigned char byte;
                do { check(at < bytes.size(), "truncated bin ID"); byte = bytes[at++]; id = (id << 7) | (byte & 127); } while (byte & 128);
            }
            if (present >= 2) cls = static_cast<int>(vbas(bytes, at));
            if (present == 3) stream = static_cast<int>(vbas(bytes, at));
            check(cls >= 0 && stream >= 0, "missing initial JPP identifiers");
            size_t offset = vbas(bytes, at), length = vbas(bytes, at);
            check(length <= bytes.size() - at, "truncated JPP payload");
            Key key(cls, stream, static_cast<int>(id));
            Bytes &bin = bins[key];
            check(offset == bin.size(), "duplicate or missing bin bytes");
            check(!complete[key], "message after final bin byte");
            check(length != 0 || (first & 16), "empty non-final message");
            bin.insert(bin.end(), bytes.begin() + at, bytes.begin() + at + length);
            complete[key] = (first & 16) != 0;
            at += length;
            ++messages;
        }
        check(false, "missing EOR");
        return -1;
    }
};

static Bytes generate(jpip::DataBinServer &session, MemorySources &sources, int capacity) {
    Bytes response;
    bool last = false;
    for (int calls = 0; !last; ++calls) {
        check(calls < 10000, "response failed to make progress");
        std::vector<char> buffer(capacity);
        int length = capacity;
        check(session.GenerateChunk(sources, buffer.data(), &length, &last), session.GetError().c_str());
        check(length >= 0 && length <= capacity && (length || last), "invalid output length");
        for (int i = 0; i < length; ++i)
            response.push_back(static_cast<unsigned char>(buffer[i]));
    }
    return response;
}

static jpip::ResponseRequest request(int width, int limit) {
    jpip::ResponseRequest r;
    r.has.fsiz = true;
    r.resolution_size = jpip::Size(width, width);
    r.has.len = true;
    r.length_response = limit;
    return r;
}

static void verify_jp2(int capacity, int limit = 256) {
    MemorySources sources;
    sources.load("jpx-graph-frame2.jp2");
    const std::string path = "./jpx-graph-frame2.jp2";
    jpip::ImageIndex image(path);
    check(image.Open(*sources.GetSource(path), sources, false), image.GetError().c_str());
    check(image.GetIndexedPackets(0) == 0, "opening decoded PLT entries");
    // Known corpus fixture: four layers, packet lengths 1,127,128,129,
    // split across two tile-parts. Validate exact ranges across fresh mappings.
    const int lengths[] = {1,127,128,129}, offsets[] = {0,1,156,284};
    uint64_t start = image.GetMainHeader(0).offset + image.GetMainHeader(0).length + 26;
    const int order[] = {0,2,1,3};
    for (int layer : order) {
        sources.remap();
        jpip::FileSegment segment;
        check(image.GetPacket(sources.GetSource(path), 0,
                    jpip::Packet(layer,0,0,jpip::Point()), &segment), image.GetError().c_str());
        check(segment.offset == start + offsets[layer] && segment.length == static_cast<uint64_t>(lengths[layer]), "wrong packet range");
        check(image.GetIndexedPackets(0) == (layer == 0 ? 1 : layer == 3 ? 4 : 3), "lazy index advanced incorrectly");
    }
    Bytes expected_packet;
    const Bytes file = sources.bytes[path];
    for (int i = 0; i < 4; ++i)
        expected_packet.insert(expected_packet.end(), file.begin()+start+offsets[i], file.begin()+start+offsets[i]+lengths[i]);
    // Independently form bin 0 from the original box header and a phld box.
    Bytes expected_meta;
    size_t cursor = 0;
    while (cursor < file.size()) {
        size_t box = cursor;
        uint64_t size = number(file, cursor, 4), type = number(file, cursor, 4);
        check(size >= 8 && size <= file.size()-box, "bad fixture box");
        if (type != 0x6a703263) expected_meta.insert(expected_meta.end(),file.begin()+box,file.begin()+box+size);
        else {
            put(expected_meta,52,4); put(expected_meta,0x70686c64,4); put(expected_meta,4,4); put(expected_meta,0,8);
            expected_meta.insert(expected_meta.end(),file.begin()+box,file.begin()+box+8);
            put(expected_meta,0,8); put(expected_meta,0,8); put(expected_meta,0,8);
        }
        cursor = box+size;
    }
    jpip::DataBinServer session;
    Received received;
    jpip::ResponseRequest r = request(4096,limit);
    for (int attempts = 0;; ++attempts) {
        check(attempts < 1000, "byte-limited request did not finish");
        check(session.SetRequest(image,r), "request rejected");
        sources.remap();
        int messages_before = received.messages;
        Bytes bytes = generate(session,sources,capacity);
        check(bytes.size() <= static_cast<size_t>(limit), "response exceeded byte limit");
        int reason = received.decode(bytes);
        if (reason == jpip::EOR::WINDOW_DONE) break;
        check(received.messages > messages_before, "byte-limited response made no progress");
        check(reason == jpip::EOR::BYTE_LIMIT_REACHED, "wrong completion reason");
    }
    check(received.bins[Key(0,0,0)] == expected_packet && received.complete[Key(0,0,0)], "wrong precinct bin");
    check(received.bins[Key(8,0,0)] == expected_meta && received.complete[Key(8,0,0)], "wrong metadata bin");
    const jpip::FileSegment &header=image.GetMainHeader(0);
    check(received.bins[Key(6,0,0)] == Bytes(file.begin()+header.offset,file.begin()+header.offset+header.length), "wrong main header");
    check(received.complete[Key(2,0,0)] && received.bins[Key(2,0,0)].empty(), "missing empty final tile header");
    check(session.SetRequest(image,r), "repeat rejected");
    Bytes repeat = generate(session,sources,capacity);
    check(repeat == Bytes({0,2,0}), "repeat resent cached content");
    jpip::ResponseRequest bad = r;
    bad.has.model = true;
    bad.model.push_back({jpip::META_DATA,false,0,0,999,INT_MAX});
    check(!session.SetRequest(image,bad), "invalid cache model accepted");
    check(session.SetRequest(image,r) && generate(session,sources,capacity) == repeat, "rejection modified cache");
    // Start another client with partial prefixes, including one ending inside
    // a generated placeholder. Reconstruct exactly the same complete bins.
    jpip::DataBinServer partial;
    Received suffix;
    jpip::ResponseRequest cached = r;
    cached.has.model = true;
    for (const auto &entry : received.bins) {
        const Key &key = entry.first;
        size_t prefix = entry.second.size()/2;
        if (std::get<0>(key) == jpip::META_DATA) prefix = entry.second.size()-7;
        int amount = entry.second.empty() ? INT_MAX : static_cast<int>(prefix);
        cached.model.push_back({static_cast<jpip::DataBinClass>(std::get<0>(key)),true,
                               std::get<1>(key),std::get<1>(key),std::get<2>(key),amount});
        suffix.bins[key] = Bytes(entry.second.begin(),entry.second.begin()+prefix);
        suffix.complete[key] = entry.second.empty();
    }
    for (int attempts=0;;++attempts) {
        check(attempts<1000 && partial.SetRequest(image,cached), "partial cache rejected");
        sources.remap();
        int messages_before = suffix.messages;
        int reason = suffix.decode(generate(partial,sources,capacity));
        cached.has.model = false;
        if(reason == jpip::EOR::WINDOW_DONE) break;
        check(suffix.messages > messages_before, "partial response made no progress");
        check(reason == jpip::EOR::BYTE_LIMIT_REACHED, "partial cache completion reason");
    }
    check(suffix.bins == received.bins && suffix.complete == received.complete, "partial cache changed reconstructed bins");
    jpip::DataBinServer small;
    check(small.SetRequest(image,r), "small-buffer setup failed");
    char buffer[2]; int size=2; bool last=false;
    check(!small.GenerateChunk(sources,buffer,&size,&last) && size==0 && !small.GetError().empty(), "small buffer not diagnosed");
}

static void verify_zoom_and_links() {
    MemorySources sources;
    sources.load("jp2-precincts.jp2");
    jpip::ImageIndex image("./jp2-precincts.jp2");
    check(image.Open(*sources.GetSource(image.GetPathName()),sources,false),image.GetError().c_str());
    const auto *parameters = image.GetCodingParameters(0);
    check(parameters->num_levels > 0,"zoom fixture has no resolution hierarchy");
    jpip::DataBinServer session;
    Received accumulated;
    auto low = request(1,INT_MAX);
    check(session.SetRequest(image,low),"low resolution rejected");
    accumulated.decode(generate(session,sources,128));
    int before=accumulated.messages;
    check(session.SetRequest(image,low) && generate(session,sources,128)==Bytes({0,2,0}),"playback repeat retransmitted data");
    auto full = request(parameters->size.x,INT_MAX);
    check(session.SetRequest(image,full),"zoom rejected");
    sources.remap();
    accumulated.decode(generate(session,sources,128));
    check(accumulated.messages>before,"zoom sent no additional bins");
    jpip::DataBinServer fresh;
    Received reference;
    check(fresh.SetRequest(image,full),"fresh full window rejected");
    reference.decode(generate(fresh,sources,1024));
    check(accumulated.bins==reference.bins && accumulated.complete==reference.complete,"zoom lost or duplicated bin data");

    sources.load("jpx-graph-repeated.jpx");
    sources.load("jpx-linked-frame1.jp2");
    sources.load("jpx-graph-frame2.jp2");
    jpip::ImageIndex movie("./jpx-graph-repeated.jpx");
    check(movie.Open(*sources.GetSource(movie.GetPathName()),sources,true),movie.GetError().c_str());
    check(movie.GetNumCodestreams()==2,"wrong linked movie size");
    check(sources.released==2,"opening retained linked sources");
    full.AddStream(0,1);
    jpip::DataBinServer playback;
    Received frames;
    check(playback.SetRequest(movie,full),"movie request rejected");
    sources.remap();
    frames.decode(generate(playback,sources,128));
    check(frames.bins[Key(0,0,0)].size()==385 && frames.bins[Key(0,0,0)]==frames.bins[Key(0,1,0)],"linked sources not mapped to separate bins");
    check(frames.complete[Key(0,0,0)] && frames.complete[Key(0,1,0)],"linked bins incomplete");
    full.AddStream(0,1,0);
    check(!playback.SetRequest(movie,full),"zero stream-selection step accepted");
}

static void verify_argument_recovery() {
    jpip::CacheModel cache;
    check(cache.GetDataBin(jpip::EXTENDED_PRECINCT,0,0)==-1 &&
          cache.AddToDataBin(jpip::EXTENDED_PRECINCT,0,0,1)==-1 &&
          cache.AugmentDataBin(jpip::EXTENDED_PRECINCT,0,0,1)==-1 &&
          cache.GetDataBin(jpip::PRECINCT,0,0)==0,
          "unsupported cache operation changed state or lacked an error");
    jpip::CodingParameters invalid;
    invalid.progression = -1;
    check(invalid.GetProgressionIndex(jpip::Packet(0,0,0,jpip::Point()))==-1,
          "unsupported progression lacked an error");
    MemorySources sources;
    sources.load("jp2.jp2");
    jpip::ImageIndex image("./jp2.jp2");
    const jpip::Source *source = sources.GetSource(image.GetPathName());
    check(image.Open(*source,sources,false), image.GetError().c_str());
    jpip::FileSegment range;
    const jpip::Packet valid(0,0,0,jpip::Point());
    check(!image.GetPacket(source,0,jpip::Packet(-1,0,0,jpip::Point()),&range),
          "invalid packet accepted");
    check(!image.GetError().empty() && image.GetIndexedPackets(0)==0,
          "argument rejection changed the index or lost its error");
    check(image.GetPacket(source,0,valid,&range) && image.GetError().empty(),
          "argument rejection poisoned the index");
    check(!image.GetPacket(NULL,0,valid,&range) && image.GetPacket(source,0,valid,&range),
          "missing caller source poisoned the index");
}

static void verify_deferred_error() {
    MemorySources sources;
    const std::string name = "jp2-rule-plt.sum-exceeds-data-23.jp2";
    sources.load(name);
    jpip::ImageIndex image("./" + name);
    check(image.Open(*sources.GetSource(image.GetPathName()), sources, false),
          "deferred PLT fixture failed structural open");
    jpip::DataBinServer session;
    check(session.SetRequest(image, request(4096, 64000)), "PLT request rejected");
    char buffer[64000];
    int length = sizeof(buffer);
    bool last = false;
    check(!session.GenerateChunk(sources, buffer, &length, &last),
          "malformed PLT produced a successful response");
    check(image.GetError().find("plt.coverage") != std::string::npos &&
          session.GetError() == image.GetError(), "deferred PLT diagnostic was lost");
}

static void verify_typed_requests() {
    MemorySources sources;
    sources.load("jpx-graph-repeated.jpx");
    sources.load("jpx-linked-frame1.jp2");
    sources.load("jpx-graph-frame2.jp2");
    jpip::ImageIndex image("./jpx-graph-repeated.jpx");
    check(image.Open(*sources.GetSource(image.GetPathName()), sources, true),
          "typed request fixture failed");
    jpip::ResponseRequest r = request(4096, INT_MAX);
    r.AddContext(0, 0);
    r.AddStream(1, 1);
    r.AddStream(0, 0); // Does not change the first stream's model default.
    r.has.model = true;
    r.model.push_back({jpip::PRECINCT, false, 0, 0, 0, INT_MAX});
    int stream = -1;
    check(r.GetUnqualifiedModelCodestream(2, &stream) && stream == 1,
          "typed request applied the model to the wrong stream");
    std::vector<int> selected;
    check(r.SelectCodestreams(2, &selected) && selected == std::vector<int>({0, 1}),
          "typed request lost context/stream order or deduplication");
    jpip::DataBinServer session;
    check(session.SetRequest(image, r), "typed model rejected");
    Received received;
    check(received.decode(generate(session, sources, 128)) == jpip::EOR::WINDOW_DONE,
          "typed model did not complete");
    check(received.bins[Key(jpip::PRECINCT, 0, 0)].size() == 385 &&
          received.bins.count(Key(jpip::PRECINCT, 1, 0)) == 0,
          "unqualified model suppressed the wrong codestream");

    jpip::ResponseRequest context;
    context.AddContext(1, 1);
    check(context.GetUnqualifiedModelCodestream(2, &stream) && stream == 0,
          "context changed the default for an unqualified model");
    jpip::ResponseRequest outside;
    outside.AddStream(9, 9);
    outside.AddStream(1, 1);
    check(!outside.GetUnqualifiedModelCodestream(2, &stream),
          "model default changed to the first available stream");

    // Reject malformed typed values before applying their otherwise valid
    // cache update. The next valid request must still send stream 1.
    jpip::DataBinServer unchanged;
    for (int kind = 0; kind < 3; ++kind) {
        jpip::ResponseRequest bad = request(4096, INT_MAX);
        bad.AddStream(1, 1);
        bad.has.model = true;
        bad.model.push_back({jpip::PRECINCT, false, 0, 0, 0, INT_MAX});
        if (kind == 0) bad.AddStream(3, 2);
        else if (kind == 1) bad.length_response = -1;
        else bad.model.push_back({jpip::MAIN_HEADER, false, 0, 0, 1, INT_MAX});
        std::string error;
        check(!unchanged.SetRequest(image, bad, &error) && !error.empty(),
              "malformed typed request accepted without a diagnostic");
    }
    jpip::ResponseRequest valid = request(4096, INT_MAX);
    valid.AddStream(1, 1);
    check(unchanged.SetRequest(image, valid), "request after rejection failed");
    Received fresh;
    fresh.decode(generate(unchanged, sources, 128));
    check(fresh.bins[Key(jpip::PRECINCT, 1, 0)].size() == 385,
          "a rejected request modified the cache");
}

static void verify_source_failures() {
    const std::string name = "jpx-graph-frame2.jp2", path = "./" + name;
    // Fail acquisition while generating metadata, headers, or packets.
    for (int phase = 0; phase < 3; ++phase) {
        MemorySources sources;
        sources.load(name);
        sources.load("jpx-linked-frame1.jp2");
        sources.load("jpx-graph-repeated.jpx");
        const std::string root = phase == 0 ? path : "./jpx-graph-repeated.jpx";
        jpip::ImageIndex image(root);
        check(image.Open(*sources.GetSource(root), sources, phase != 0), "source fixture failed");
        jpip::ResponseRequest r = request(4096, INT_MAX);
        r.has.model = true;
        if (phase >= 1) r.model.push_back({jpip::META_DATA, false, 0, 0, 0, INT_MAX});
        if (phase == 2) {
            r.model.push_back({jpip::MAIN_HEADER, false, 0, 0, 0, INT_MAX});
            r.model.push_back({jpip::TILE_HEADER, false, 0, 0, 0, INT_MAX});
        }
        jpip::DataBinServer session;
        check(session.SetRequest(image, r), "missing-source setup failed");
        sources.views.clear();
        sources.bytes.erase(path);
        sources.bytes.erase("./jpx-linked-frame1.jp2");
        const std::string missing = phase == 0 ? root : image.GetPathName(0);
        char buffer[64000]; int length = sizeof buffer; bool last = false;
        check(!session.GenerateChunk(sources, buffer, &length, &last) &&
              session.GetError().find(missing) != std::string::npos,
              "source acquisition failure lost its source identity");
    }

    // A linked source that opens but fails validation must also be released.
    MemorySources sources;
    sources.load("jpx-graph-repeated.jpx");
    sources.load("jpx-graph-frame2.jp2");
    for (const char *companion : {"jpx-linked-frame1.jp2", "jpx-graph-frame2.jp2"})
        sources.bytes[std::string("./") + companion] = Bytes(12, 0);
    jpip::ImageIndex image("./jpx-graph-repeated.jpx");
    check(!image.Open(*sources.GetSource(image.GetPathName()), sources, true) &&
          sources.released == 1 && image.GetError().find(".jp2:") != std::string::npos,
          "invalid linked source was retained or reported as the root JPX");
}

// Small independent wire fixtures: two resolutions, two components, two layers,
// six precincts per resolution, and a distinct byte for every packet.
struct Synthetic {
    Bytes codestream, header;
    std::map<int,Bytes> precincts;
};
static unsigned char packet_byte(int l,int r,int c,int x,int y) {
    return static_cast<unsigned char>(1+l+2*c+4*r+8*y+24*x);
}
static void box(Bytes &out,uint32_t type,const Bytes &body) {
    put(out,8+body.size(),4); put(out,type,4);
    out.insert(out.end(),body.begin(),body.end());
}
static Synthetic synthetic(int progression) {
    struct Packet { int l,r,c,x,y; };
    std::vector<Packet> packets;
    Synthetic result;
    for(int r=0;r<2;++r)for(int y=0;y<2;++y)for(int x=0;x<3;++x)for(int c=0;c<2;++c)
        for(int l=0;l<2;++l) {
            packets.push_back({l,r,c,x,y});
            result.precincts[c+2*(6*r+3*y+x)].push_back(packet_byte(l,r,c,x,y));
        }
    // Both resolutions have reference-grid precinct anchors (2*x,2*y).
    auto key=[progression](const Packet &p) {
        switch(progression) {
        case 0:return std::make_tuple(p.l,p.r,p.c,p.y,p.x);
        case 1:return std::make_tuple(p.r,p.l,p.c,p.y,p.x);
        case 2:return std::make_tuple(p.r,p.y,p.x,p.c,p.l);
        case 3:return std::make_tuple(p.y,p.x,p.c,p.r,p.l);
        default:return std::make_tuple(p.c,p.y,p.x,p.r,p.l);
        }
    };
    std::sort(packets.begin(),packets.end(),[&](const Packet &a,const Packet &b){return key(a)<key(b);});
    Bytes &out=result.codestream;
    put(out,0xff4f,2); put(out,0xff51,2); put(out,44,2); put(out,0,2);
    for(unsigned v : {5u,3u,0u,0u,5u,3u,0u,0u})put(out,v,4);
    put(out,2,2);
    for(int c=0;c<2;++c)out.insert(out.end(),{7,1,1});
    put(out,0xff52,2);put(out,14,2);put(out,1,1);put(out,progression,1);put(out,2,2);
    out.insert(out.end(),{0,1,0,0,0,1,0x00,0x11});
    put(out,0xff5c,2);put(out,7,2);out.insert(out.end(),{0,0,0,0,0});
    result.header=out;
    for(int tile=0;tile<2;++tile) {
        int padding=tile==1 ? 7 : 0;
        put(out,0xff90,2);put(out,10,2);put(out,0,2);put(out,19+48+padding,4);
        put(out,tile,1);put(out,2,1);
        put(out,0xff58,2);put(out,3+24+padding,2);put(out,0,1);
        for(int i=0;i<24;++i)put(out,1,1);
        for(int i=0;i<padding;++i)put(out,0,1);
        put(out,0xff93,2);
        for(int i=tile*24;i<(tile+1)*24;++i) {
            const Packet &p=packets[i];put(out,packet_byte(p.l,p.r,p.c,p.x,p.y),1);
        }
    }
    put(out,0xffd9,2);
    return result;
}
static void fetch(jpip::DataBinServer &session,jpip::ImageIndex &image,
                  MemorySources &sources,const jpip::ResponseRequest &req,Received &received) {
    for(int i=0;i<1000;++i) {
        check(session.SetRequest(image,req),"synthetic request rejected");
        int before=received.messages;
        Bytes bytes=generate(session,sources,128);
        check(!req.has.len || bytes.size()<=static_cast<size_t>(req.length_response),"synthetic response exceeded len");
        int reason=received.decode(bytes);
        if(reason==jpip::EOR::WINDOW_DONE)return;
        check(reason==jpip::EOR::BYTE_LIMIT_REACHED && received.messages>before,"synthetic continuation stalled");
    }
    check(false,"synthetic continuation did not complete");
}
static void verify_synthetic() {
    for(int progression=0;progression<5;++progression)for(bool jpx : {false,true}) {
        std::vector<Synthetic> streams{synthetic(progression)};
        if(jpx)streams.push_back(synthetic((progression+1)%5));
        Bytes file,signature,ftyp;
        put(signature,0x0d0a870a,4);box(file,0x6a502020,signature);
        uint32_t brand=jpx?0x6a707820:0x6a703220;
        put(ftyp,brand,4);put(ftyp,0,4);put(ftyp,brand,4);box(file,0x66747970,ftyp);
        Bytes metadata=file;
        std::map<Key,Bytes> expected;
        for(size_t cs=0;cs<streams.size();++cs) {
            if(jpx){box(file,0x6a706368,{});box(metadata,0x6a706368,{});}
            Bytes header;put(header,8+streams[cs].codestream.size(),4);put(header,0x6a703263,4);
            box(file,0x6a703263,streams[cs].codestream);
            put(metadata,52,4);put(metadata,0x70686c64,4);put(metadata,4,4);put(metadata,0,8);
            metadata.insert(metadata.end(),header.begin(),header.end());
            put(metadata,0,8);put(metadata,0,8);put(metadata,cs,8);
            expected[Key(6,cs,0)]=streams[cs].header;
            expected[Key(2,cs,0)]={};
            for(const auto &bin:streams[cs].precincts)expected[Key(0,cs,bin.first)]=bin.second;
        }
        expected[Key(8,0,0)]=metadata;
        MemorySources sources;sources.bytes["./synthetic"]=file;
        jpip::ImageIndex image("./synthetic");
        check(image.Open(*sources.GetSource(image.GetPathName()),sources,jpx),image.GetError().c_str());
        jpip::ResponseRequest full=request(5,100);full.resolution_size=jpip::Size(5,3);
        full.AddStream(0,streams.size()-1);
        std::map<Key,Bytes> header_bins;
        for(const auto &bin:expected)if(std::get<0>(bin.first)!=0)header_bins.insert(bin);

        jpip::DataBinServer layer_session;
        Received layer_bins;
        jpip::ResponseRequest limited = full;
        limited.layers = 0;
        fetch(layer_session, image, sources, limited, layer_bins);
        check(layer_bins.bins == header_bins, "Zero layers emitted precinct packets or omitted headers");
        for (size_t cs = 0; cs < streams.size(); ++cs)
            check(image.GetIndexedPackets(cs) == 0, "Zero layers indexed packets");
        limited.layers = 1;
        fetch(layer_session, image, sources, limited, layer_bins);
        std::map<Key,Bytes> one_layer = header_bins;
        for (const auto &bin : expected)
            if (std::get<0>(bin.first) == 0) {
                one_layer[bin.first] = Bytes(bin.second.begin(), bin.second.begin() + 1);
                check(!layer_bins.complete[bin.first], "Requested layer incorrectly completed a precinct");
            }
        check(layer_bins.bins == one_layer, "One layer differs from independent packet bytes");
        int messages = layer_bins.messages;
        for (uint64_t limit : {uint64_t(0), uint64_t(1)}) {
            limited.layers = limit;
            fetch(layer_session, image, sources, limited, layer_bins);
            check(layer_bins.messages == messages, "Lower or repeated layer limit sent more data");
        }
        limited.layers = UINT64_MAX;
        fetch(layer_session, image, sources, limited, layer_bins);
        check(layer_bins.bins == expected, "Increasing layers lost or duplicated packet bytes");
        for (const auto &bin : expected)
            check(layer_bins.complete[bin.first], "All layers did not complete a bin");
        messages = layer_bins.messages;
        for (uint64_t limit : {uint64_t(1), uint64_t(2), uint64_t(65536), uint64_t(UINT64_MAX)}) {
            limited.layers = limit;
            fetch(layer_session, image, sources, limited, layer_bins);
            check(layer_bins.messages == messages, "Cached layer changes retransmitted data");
        }

        jpip::DataBinServer session;Received received;
        fetch(session,image,sources,full,received);
        check(received.bins==expected,"synthetic exact bin bytes or IDs differ");
        for(const auto &bin:expected)check(received.complete[bin.first],"synthetic final flag missing");
        jpip::DataBinServer metadata_session;Received headers;
        jpip::ResponseRequest metadata_request=full;metadata_request.has.fsiz=false;
        fetch(metadata_session,image,sources,metadata_request,headers);
        check(headers.bins==header_bins,"metadata-only response contains wrong bins");
        jpip::DataBinServer empty_session;Received empty;
        jpip::ResponseRequest empty_request=full;
        empty_request.has.rsiz=true;empty_request.woi_size=jpip::Size(0,3);
        fetch(empty_session,image,sources,empty_request,empty);
        check(empty.bins.empty(),"empty window emitted data");
        jpip::DataBinServer zoom_session;Received zoom;
        jpip::ResponseRequest low=full;low.resolution_size=jpip::Size(3,2);
        fetch(zoom_session,image,sources,low,zoom);
        std::map<Key,Bytes> low_bins;
        for(const auto &bin:expected)if(std::get<0>(bin.first)!=0 || std::get<2>(bin.first)<12)low_bins.insert(bin);
        check(zoom.bins==low_bins,"low resolution differs from independent bins");
        sources.remap();
        fetch(zoom_session,image,sources,full,zoom);
        check(zoom.bins==expected,"zoom differs from independent full bins");
        jpip::DataBinServer crop_session;Received crop;
        jpip::ResponseRequest cropped=full;
        cropped.has.roff=cropped.has.rsiz=true;
        cropped.woi_position=jpip::Point(4,0);cropped.woi_size=jpip::Size(1,3);
        fetch(crop_session,image,sources,cropped,crop);
        std::map<Key,Bytes> cropped_bins;
        for(const auto &bin:expected)if(std::get<0>(bin.first)!=0 || (std::get<2>(bin.first)/2)%3==2)cropped_bins.insert(bin);
        check(crop.bins==cropped_bins,"cropped window differs from independent precinct IDs");
    }
}

static void verify_layer_fragments() {
    MemorySources sources;
    sources.load("jpx-graph-frame2.jp2");
    jpip::ImageIndex image("./jpx-graph-frame2.jp2");
    check(image.Open(*sources.GetSource(image.GetPathName()), sources, false), image.GetError().c_str());
    const Bytes &file = sources.bytes[image.GetPathName()];
    uint64_t start = image.GetMainHeader(0).offset + image.GetMainHeader(0).length + 26;
    const int lengths[] = {1, 127, 128, 129}, offsets[] = {0, 1, 156, 284};
    Bytes expected;
    for (int i = 0; i < 4; ++i)
        expected.insert(expected.end(), file.begin() + start + offsets[i],
                        file.begin() + start + offsets[i] + lengths[i]);

    jpip::DataBinServer session;
    Received received;
    jpip::ResponseRequest r = request(4096, INT_MAX);
    r.layers = 0;
    fetch(session, image, sources, r, received);
    r.layers = 3;
    r.length_response = 220;
    check(session.SetRequest(image, r), "Fragmented layer request rejected");
    check(received.decode(generate(session, sources, 128)) == jpip::EOR::BYTE_LIMIT_REACHED,
          "Fragmented layer request did not reach its byte limit");
    Key precinct(0, 0, 0);
    size_t prefix = received.bins[precinct].size();
    check(prefix > 1 && prefix < 256 && !received.complete[precinct] &&
          received.bins[precinct] == Bytes(expected.begin(), expected.begin() + prefix),
          "Fragmented response has wrong layer prefix or completion flag");

    int messages = received.messages;
    r.layers = 1;
    r.length_response = INT_MAX;
    fetch(session, image, sources, r, received);
    check(received.messages == messages, "Lower layer limit augmented a partially cached precinct");
    r.layers = 3;
    r.length_response = 220;
    sources.remap();
    fetch(session, image, sources, r, received);
    check(received.bins[precinct] == Bytes(expected.begin(), expected.begin() + 256) &&
          !received.complete[precinct], "Continuation exceeded or missed the third layer boundary");
    r.layers = UINT64_MAX;
    fetch(session, image, sources, r, received);
    check(received.bins[precinct] == expected && received.complete[precinct],
          "Final source layer did not finish the fragmented precinct");
}

static void verify_tiny_budgets() {
    MemorySources sources;sources.load("jp2.jp2");
    jpip::ImageIndex image("./jp2.jp2");
    check(image.Open(*sources.GetSource(image.GetPathName()),sources,false),image.GetError().c_str());
    {
        jpip::DataBinServer uninitialized;
        char buffer[60];int length=sizeof buffer;bool last=false;
        check(!uninitialized.GenerateChunk(sources,buffer,&length,&last) && length==0 &&
              uninitialized.GetError()=="No request", "missing request was not diagnosed");
    }
    jpip::DataBinServer fresh;
    check(fresh.SetRequest(image,request(4096,INT_MAX)),"reference request rejected");
    Bytes expected=generate(fresh,sources,64000);
    for(int budget=0;budget<=60;++budget) {
        jpip::DataBinServer session;
        check(session.SetRequest(image,request(4096,budget)),"tiny request rejected");
        check(generate(session,sources,64000)==Bytes({0,jpip::EOR::BYTE_LIMIT_REACHED,0}),
              "tiny budget did not return byte-limit EOR alone");
        check(session.SetRequest(image,request(4096,INT_MAX)),"continuation rejected");
        check(generate(session,sources,64000)==expected,
              "EOR-only response changed subsequent data or cache state");
    }
    for(int budget : {4,60,61,100,INT_MAX}) {
        for(int capacity : {2,3,60}) {
            if(capacity>=budget)continue;
            jpip::DataBinServer session;
            check(session.SetRequest(image,request(4096,budget)),"small-buffer request rejected");
            char buffer[60];int length=capacity;bool last=false;
            check(!session.GenerateChunk(sources,buffer,&length,&last) &&
                  length==0 && !last && !session.GetError().empty(),
                  "undersized output buffer was not diagnosed");
        }
    }
}

static void verify_interleaved_sessions() {
    MemorySources sources;
    sources.load("jpx-graph-frame2.jp2");
    const std::string path = "./jpx-graph-frame2.jp2";
    jpip::ImageIndex first_image(path), second_image(path), reference_image(path);
    for (jpip::ImageIndex *image : {&first_image, &second_image, &reference_image})
        check(image->Open(*sources.GetSource(path), sources, false), "interleaved fixture failed");
    jpip::ResponseRequest full = request(4096, INT_MAX);
    jpip::DataBinServer reference;
    Received expected;
    fetch(reference, reference_image, sources, full, expected);

    jpip::DataBinServer first, second;
    Received first_bins, second_bins;
    jpip::ResponseRequest small = request(4096, 100);
    check(first.SetRequest(first_image, small) && second.SetRequest(second_image, full),
          "interleaved requests rejected");
    MemorySources failing_sources;
    failing_sources.load("jpx-graph-frame2.jp2");
    jpip::ImageIndex failing_image(path);
    check(failing_image.Open(*failing_sources.GetSource(path), failing_sources, false),
          "failure fixture rejected");
    jpip::DataBinServer failing;
    check(failing.SetRequest(failing_image, full), "failure setup rejected");
    failing_sources.views.clear();
    failing_sources.bytes.erase(path);
    bool first_last = false, second_last = false;
    Bytes first_response, second_response;
    // Both channels have outstanding responses. The provider keeps borrowed
    // sources alive until both responses finish, as its contract requires.
    for (int calls = 0; !first_last || !second_last; ++calls) {
        check(calls < 1000, "interleaved generation stalled");
        if (calls == 1) {
            check(!second_last, "Fixture did not keep a response active through failure");
            char buffer[128]; int length = sizeof buffer; bool last = false;
            check(!failing.GenerateChunk(failing_sources, buffer, &length, &last) &&
                  failing.GetError().find(path) != std::string::npos,
                  "controlled failure in another session lost its identity");
        }
        for (int which : {0,1}) {
            bool &last = which == 0 ? first_last : second_last;
            if (last) continue;
            jpip::DataBinServer &session = which == 0 ? first : second;
            Bytes &response = which == 0 ? first_response : second_response;
            char buffer[128]; int length = sizeof buffer;
            check(session.GenerateChunk(sources, buffer, &length, &last), session.GetError().c_str());
            check(length > 0 && length <= static_cast<int>(sizeof buffer), "invalid interleaved chunk");
            for (int i = 0; i < length; ++i) response.push_back(static_cast<unsigned char>(buffer[i]));
        }
    }
    check(first_bins.decode(first_response) == jpip::EOR::BYTE_LIMIT_REACHED &&
          second_bins.decode(second_response) == jpip::EOR::WINDOW_DONE &&
          second_bins.bins == expected.bins && second_bins.complete == expected.complete,
          "interleaving changed an independent channel's bins or completion");
    sources.remap();
    fetch(first, first_image, sources, small, first_bins);
    check(first_bins.bins == expected.bins && first_bins.complete == expected.complete,
          "an independent cache changed the other channel's continuation");
    int first_messages = first_bins.messages, second_messages = second_bins.messages;
    fetch(first, first_image, sources, full, first_bins);
    fetch(second, second_image, sources, full, second_bins);
    check(first_bins.messages == first_messages && second_bins.messages == second_messages,
          "completed independent sessions resent cached data");


}

int main() {
    verify_interleaved_sessions();
    verify_synthetic();
    verify_tiny_budgets();
    verify_layer_fragments();
    verify_typed_requests();
    verify_source_failures();
    verify_deferred_error();
    verify_argument_recovery();
    verify_zoom_and_links();
    for (int capacity : {128,255,256,1024}) verify_jp2(capacity);
    for (int limit : {61,62,100,111,112}) verify_jp2(64000, limit);
    std::cout << "PASS: memory sources, remapping, lazy ranges, reconstructed bins, byte limits and cache continuation\n";
}
