// Included by server_test.cc to reuse its live-server and HTTP harness.
// The decoder follows T.808 A.2 and D.3; it uses no production JPIP code.
namespace jpp_test {

using Key = tuple<uint64_t, uint64_t, uint64_t>; // class, codestream, bin
using Expected = map<Key, string>;

void Require(bool condition, const char *message) {
    if (!condition) throw runtime_error(message);
}

struct Bin {
    string bytes;
    bool complete = false;
};

class Cache {
    const Expected &expected;
    map<Key, Bin> bins;

    static unsigned Byte(const string &body, size_t &position) {
        Require(position < body.size(), "Truncated JPP message");
        return static_cast<unsigned char>(body[position++]);
    }

    static uint64_t Integer(const string &body, size_t &position,
                            uint64_t value = 0, bool continuation = true) {
        while (continuation) {
            unsigned byte = Byte(body, position);
            Require(value <= (UINT64_MAX >> 7), "Overflowing JPP VBAS");
            value = (value << 7) | (byte & 127);
            continuation = (byte & 128) != 0;
        }
        return value;
    }

public:
    explicit Cache(const Expected &_expected) : expected(_expected) {}

    void Seed(const Key &key, size_t length, bool complete = false) {
        Require(length <= expected.at(key).size(), "Invalid test cache seed");
        bins[key].bytes = expected.at(key).substr(0, length);
        bins[key].complete = complete;
    }

    size_t Size() const {
        size_t result = 0;
        for (const auto &entry : bins) result += entry.second.bytes.size();
        return result;
    }

    bool Complete(const Expected &selection) const {
        for (const auto &entry : selection) {
            auto found = bins.find(entry.first);
            if (found == bins.end() || !found->second.complete ||
                found->second.bytes != entry.second) return false;
        }
        return true;
    }

    bool Complete() const { return Complete(expected); }

    unsigned Read(const string &body, const Expected *selection = NULL) {
        // Class and CSn default to zero at the start of each response.
        uint64_t cls = 0, stream = 0;
        size_t position = 0;
        while (position < body.size()) {
            unsigned first = Byte(body, position);
            if (first == 0) {
                unsigned reason = Byte(body, position);
                uint64_t length = Integer(body, position);
                Require(length == body.size() - position, "Missing or misplaced EOR boundary");
                Require(reason == 2 || reason == 4, "Unexpected EOR reason");
                return reason;
            }
            unsigned presence = (first >> 5) & 3;
            Require(presence != 0, "Reserved JPP header form");
            uint64_t id = Integer(body, position, first & 15, (first & 128) != 0);
            if (presence >= 2) cls = Integer(body, position);
            if (presence == 3) stream = Integer(body, position);
            uint64_t offset = Integer(body, position);
            uint64_t length = Integer(body, position);
            Require((cls & 1) == 0, "Unexpected extended data-bin class");
            Key key(cls, stream, id);
            auto source = expected.find(key);
            Require(source != expected.end(), "Unexpected data-bin identifier");
            Require(selection == NULL || selection->count(key), "Unrequested data-bin");
            Require(length <= body.size() - position && offset <= source->second.size() &&
                    length <= source->second.size() - offset, "JPP message exceeds bin or response");
            Bin &bin = bins[key];
            // These additive-model runs request each missing suffix once.
            // Equality also catches ignored models that resend seeded prefixes.
            Require(offset == bin.bytes.size(), "Gap or retransmission in reconstructed data-bin");
            Require(!bin.complete || offset + length <= bin.bytes.size(), "Data after completed bin");
            Require(body.compare(position, length, source->second, offset, length) == 0,
                    "JPP payload differs from source bytes");
            bin.bytes.append(body, position, length);
            if (first & 16) {
                Require(offset + length == source->second.size(), "Premature bin completion");
                bin.complete = true;
            }
            position += length;
        }
        throw runtime_error("Response has no EOR");
    }
};

void CheckReader() {
    Expected expected = {{Key(0, 0, 200), "ab"}};
    const string valid("\xB1\x48\x00\x02" "ab\x00\x02\x00", 9);
    Cache cache(expected);
    Require(cache.Read(valid) == 2 && cache.Complete(), "JPP reader rejected canonical Bin-ID 200");
    vector<string> bad = {valid.substr(0, 8), valid.substr(0, 6), valid + "x"};
    string corrupt = valid;
    corrupt[2] = 1; bad.push_back(corrupt); // gap / range overrun
    corrupt = valid; corrupt[4] = 'z'; bad.push_back(corrupt);
    corrupt = valid; corrupt[3] = 1; corrupt.erase(5, 1); bad.push_back(corrupt);
    corrupt = valid; corrupt[0] = '\x81'; bad.push_back(corrupt); // reserved presence bits
    corrupt = valid; corrupt[7] = 99; bad.push_back(corrupt); // unsupported EOR reason
    corrupt = valid; corrupt[8] = 1; bad.push_back(corrupt); // missing EOR body
    bad.push_back(string("\xA0", 1) + string(11, '\xFF')); // overflowing Bin-ID
    for (const string &body : bad) {
        bool rejected = false;
        try { Cache probe(expected); probe.Read(body); }
        catch (const runtime_error &) { rejected = true; }
        Require(rejected, "JPP reader accepted its malformed self-test");
    }

    // Explicit class/CSn, inherited class/CSn, then class-only inheritance.
    Expected inherited = {{Key(6, 3, 0), "ab"}, {Key(6, 3, 1), "c"},
                          {Key(2, 3, 0), ""}, {Key(0, 0, 0), "d"}};
    Cache sequence(inherited);
    const char first[] = "\x70\x06\x03\x00\x02" "ab"
                         "\x31\x00\x01" "c" "\x50\x02\x00\x00\x00\x04\x00";
    Require(sequence.Read(string(first, sizeof first - 1)) == 4,
            "JPP header inheritance failed");
    const char second[] = "\x30\x00\x01" "d\x00\x02\x00";
    Require(sequence.Read(string(second, sizeof second - 1)) == 2 && sequence.Complete(),
            "JPP header defaults did not reset between responses");
}

void Number(string &bytes, uint64_t value, int width) {
    for (int i = width - 1; i >= 0; --i) bytes.push_back(value >> (8 * i));
}

string Box(const char *type, const string &payload) {
    string box;
    Number(box, payload.size() + 8, 4);
    box.append(type, 4);
    return box + payload;
}

string Preamble(const char *brand) {
    string ftyp(brand, 4);
    Number(ftyp, string(brand, 4) == "jpx " ? 1 : 0, 4); ftyp.append(brand, 4);
    return Box("jP  ", string("\x0D\x0A\x87\x0A", 4)) + Box("ftyp", ftyp);
}

// T.808 A.3.6.3: an incremental-codestream placeholder with its original
// box header, zero equivalent-bin fields, and the codestream number.
string Placeholder(const string &original, int stream) {
    string payload;
    Number(payload, 4, 4); Number(payload, 0, 8);
    payload += original.substr(0, 8);
    Number(payload, 0, 8); Number(payload, 0, 8); Number(payload, stream, 8);
    return Box("phld", payload);
}

struct Fixture {
    Expected bins;
    string linked_metadata;
};

Fixture MakeFixture(const string &directory) {
    Fixture fixture;
    string embedded = Preamble("jpx ") + Box("jpch", "") + Box("jpch", "");
    string linked = embedded;
    string embedded_metadata = embedded, linked_metadata = linked;
    // An association becomes a separate metadata bin (T.808 A.3.6.3).
    // Its nested association remains opaque within that bin.
    string association = Box("lbl ", "outer") +
            Box("asoc", Box("lbl ", "inner") + Box("xml ", "<test>" + string(257, 'x') + "</test>"));
    string asoc = Box("asoc", association);
    embedded += asoc; linked += asoc;
    string placeholder;
    Number(placeholder, 1, 4); Number(placeholder, 1, 8);
    placeholder += asoc.substr(0, 8);
    embedded_metadata += Box("phld", placeholder);
    linked_metadata += Box("phld", placeholder);
    fixture.bins[Key(8, 0, 1)] = association;
    string references;
    Number(references, 2, 2);
    for (int stream = 0; stream < 2; ++stream) {
        string header;
        Number(header, 0xFF4F, 2); Number(header, 0xFF51, 2); Number(header, 41, 2);
        Number(header, 0, 2);
        int width = 260 * (stream + 1), layers = 2 + stream;
        for (int value : {width, 1, 0, 0, width, 1, 0, 0}) Number(header, value, 4);
        Number(header, 1, 2); header.append("\x07\x01\x01", 3);
        Number(header, 0xFF52, 2); Number(header, 13, 2);
        header.append("\x01\x00\x00\x02\x00\x00\x00\x00\x00\x01\x01", 11);
        header[header.size() - 8] = layers;
        header.back() = 1 + stream; // 2- or 4-pixel precincts
        Number(header, 0xFF5C, 2); Number(header, 3, 2); header.push_back(0);
        fixture.bins[Key(6, stream, 0)] = header;
        fixture.bins[Key(2, stream, 0)] = "";
        string data, lengths;
        // Opaque packet bytes with distinct stream, layer and precinct tags.
        // This is a transport fixture, not an entropy-decoder fixture.
        for (int layer = 0; layer < layers; ++layer)
            for (int precinct = 0; precinct < 130; ++precinct) {
                string packet;
                for (int tag : {stream + 1, layer + 1, precinct, 0x5A}) packet.push_back(tag);
                // Force packet fragmentation and multi-byte offsets/lengths.
                if (precinct == 129) packet.append(128 + layer, 'a' + stream);
                if (packet.size() >= 128) lengths.push_back(0x81);
                lengths.push_back(packet.size() & 127);
                fixture.bins[Key(0, stream, precinct)] += packet;
                data += packet;
            }
        string codestream = header;
        Number(codestream, 0xFF90, 2); Number(codestream, 10, 2); Number(codestream, 0, 2);
        Number(codestream, 12 + 5 + lengths.size() + 2 + data.size(), 4);
        codestream.append("\x00\x01", 2);
        Number(codestream, 0xFF58, 2); Number(codestream, 3 + lengths.size(), 2);
        codestream.push_back(0); codestream += lengths;
        Number(codestream, 0xFF93, 2); codestream += data; Number(codestream, 0xFFD9, 2);
        string box = Box("jp2c", codestream);
        embedded += box;
        embedded_metadata += Placeholder(box, stream);
        string name = "wire-frame" + to_string(stream) + ".jp2";
        string jp2 = Preamble("jp2 ") + box;
        WriteFile(directory + "/" + name, jp2.data(), jp2.size());
        string fragment;
        Number(fragment, 1, 2); Number(fragment, 40, 8);
        Number(fragment, codestream.size(), 4); Number(fragment, stream + 1, 2);
        string ftbl = Box("ftbl", Box("flst", fragment));
        linked += ftbl;
        linked_metadata += Placeholder(ftbl, stream);
        references += Box("url ", string(4, 0) + "file://./" + name + string(1, 0));
    }
    string dtbl = Box("dtbl", references);
    linked += dtbl;
    fixture.bins[Key(8, 0, 0)] = embedded_metadata;
    WriteFile(directory + "/wire-embedded.jpx", embedded.data(), embedded.size());
    WriteFile(directory + "/wire-linked.jpx", linked.data(), linked.size());
    fixture.linked_metadata = linked_metadata + dtbl;
    return fixture;
}

int Fetch(int fd, Cache &cache, const Expected &selection, const string &target,
          const string &first_fields = "", bool gzip = false) {
    for (int i = 0; i < 300; ++i) {
        int budget = i % 2 ? 1024 : 256;
        SendRequest(fd, target + "&len=" + to_string(budget) + (i == 0 ? first_fields : ""),
                    gzip ? "Accept-Encoding: gzip\r\n" : "");
        Response response = ReadResponse(fd);
        Require(response.headers.find("200 OK") != string::npos, "JPP request failed");
        Require((response.headers.find("Content-Encoding: gzip") != string::npos) == gzip,
                "JPP response has wrong encoding");
        string body = gzip ? Gunzip(response.body) : response.body;
        Require(body.size() <= static_cast<size_t>(budget + 3), "Response exceeds len");
        size_t before = cache.Size();
        if (cache.Read(body, &selection) == 2) {
            Require(cache.Complete(selection), "Completed window has missing data");
            return i + 1;
        }
        Require(cache.Size() > before, "Response made no progress");
    }
    throw runtime_error("Response did not complete");
}

void Run(uint16_t port, const Expected &expected, const string &name,
         bool gzip, bool modeled, bool context) {
    int fd = Connect(port);
    Require(fd >= 0, "Could not connect for JPP reconstruction");
    SendRequest(fd, "/" + name + "?cnew=http&len=3");
    Response created = ReadResponse(fd);
    Require(created.headers.find("200 OK") != string::npos, "JPP channel creation failed");
    string cid = ChannelId(created.headers);
    Cache cache(expected);
    Require(cache.Read(created.body) == 4, "Initial tiny budget did not produce byte-limit EOR");
    if (modeled) {
        cache.Seed(Key(6, 0, 0), 1); cache.Seed(Key(0, 0, 0), 1);
        cache.Seed(Key(6, 1, 0), 2); cache.Seed(Key(0, 1, 129), 2);
    }
    string target = "/jpip?cid=" + cid +
            (context ? "&context=jpxl%3C0-1%3E" : "&stream=0-1") +
            "&fsiz=260,1&rsiz=260,1&roff=0,0&metareq=[*]!!";
    int requests = Fetch(fd, cache, expected, target,
                         modeled ? "&model=[0]Hm:1,P0:1,[1]Hm:2,P129:2" : "", gzip);
    Require(requests > 2, "Fixture did not exercise response continuation");
    SendRequest(fd, target + "&len=1024");
    Response repeated = ReadResponse(fd);
    Require(repeated.body.size() == 3 && cache.Read(repeated.body) == 2,
            "Repeated completed window retransmitted data");
    SendRequest(fd, "/jpip?cclose=" + cid);
    Require(ReadResponse(fd).headers.find("200 OK") != string::npos, "Could not close JPP test channel");
    close(fd);
}

Expected Select(const Expected &expected, int stream, int first, int last) {
    Expected selection;
    for (const auto &entry : expected) {
        int cls = get<0>(entry.first), cs = get<1>(entry.first), id = get<2>(entry.first);
        if (cls == 8 || (cs == stream && (cls != 0 || (id >= first && id <= last))))
            selection.insert(entry);
    }
    return selection;
}

void Stateful(uint16_t port, const Expected &expected, const string &name) {
    int fd = Connect(port);
    Require(fd >= 0, "Could not connect for stateful JPP test");
    SendRequest(fd, "/" + name + "?cnew=http&len=0");
    Response created = ReadResponse(fd);
    string cid = ChannelId(created.headers);
    Cache cache(expected);
    Require(created.body.size() == 3 && cache.Read(created.body) == 4,
            "Zero budget did not return EOR");
    string base = "/jpip?cid=" + cid;
    for (int budget : {1, 2}) {
        SendRequest(fd, base + "&len=" + to_string(budget));
        Response response = ReadResponse(fd);
        Require(response.body.size() == 3 && cache.Read(response.body) == 4,
                "Sub-EOR budget did not return EOR");
    }

    // Start inside bin 0's association placeholder and bin 1's box contents.
    cache.Seed(Key(8, 0, 0), 57); cache.Seed(Key(8, 0, 1), 9);
    Fetch(fd, cache, Select(expected, 1, -1, -1), base + "&stream=1&metareq=[*]!!",
          "&model=M0:57,M1:9", true);
    // Stream 1 is twice as wide and has an extra layer. The requested
    // 260-pixel grid still places x=258 in its final precinct, 129.
    Fetch(fd, cache, Select(expected, 1, 129, 129), base +
          "&stream=1&fsiz=260,1&rsiz=1,1&roff=258,0");
    Fetch(fd, cache, Select(expected, 0, 0, 1), base +
          "&stream=0&fsiz=260,1&rsiz=3,1&roff=0,0");
    Fetch(fd, cache, Select(expected, 1, 0, 64), base +
          "&context=jpxl%3C1%3E&fsiz=260,1&rsiz=130,1&roff=0,0");

    // Channel cache survives socket replacement, but no parser state does.
    shutdown(fd, SHUT_RDWR); close(fd);
    fd = Connect(port);
    Require(fd >= 0, "Could not reconnect to the JPP channel");
    Fetch(fd, cache, Select(expected, 1, -1, -1), base +
          "&stream=1&fsiz=260,1&rsiz=1,1&roff=260,0");
    string full = "&stream=1,0-1&fsiz=260,1"; // reversed, overlapping selection, default rsiz/roff
    Fetch(fd, cache, expected, base + full);

    // Pipelined cached windows must each finish, without resending bins or
    // treating bytes from one HTTP response as the next response's payload.
    SendRequest(fd, base + full);
    SendRequest(fd, base + "&stream=0-1:1&fsiz=260,1&rsiz=260,1&roff=0,0");
    string input;
    for (int i = 0; i < 2; ++i) {
        Response response = ReadResponse(fd, &input);
        Require(response.headers.find("200 OK") != string::npos &&
                response.body.size() == 3 && cache.Read(response.body) == 2,
                "Pipelined cached window retransmitted data");
    }

    // Browser-style socket reuse: a new channel on this same connection
    // inherits only the bins explicitly advertised, not the old channel cache.
    SendRequest(fd, "/" + name + "?cnew=http&len=3");
    Response fresh = ReadResponse(fd);
    string next = ChannelId(fresh.headers);
    Require(next != cid, "New channel reused the old identifier");
    Cache second(expected);
    Require(second.Read(fresh.body) == 4, "New channel did not start empty");
    for (const Key &key : {Key(8, 0, 0), Key(8, 0, 1), Key(6, 0, 0), Key(0, 0, 0)})
        second.Seed(key, expected.at(key).size(), true);
    Fetch(fd, second, expected, "/jpip?cid=" + next + full,
          "&model=M0,M1,[0]Hm,P0");
    SendRequest(fd, "/jpip?cclose=" + cid);
    Require(ReadResponse(fd).headers.find("200 OK") != string::npos, "Old channel close failed");
    close(fd);

    fd = Connect(port);
    Require(fd >= 0, "Could not reconnect to the independent channel");
    Fetch(fd, second, expected, "/jpip?cid=" + next + full);
    SendRequest(fd, "/jpip?cclose=" + next);
    Require(ReadResponse(fd).headers.find("200 OK") != string::npos, "New channel close failed");
    close(fd);

    // A mismatched target ID invalidates all supplied cache claims. No len
    // means unbounded, rather than inheriting the previous tiny budget.
    fd = Connect(port);
    Require(fd >= 0, "Could not connect for target-ID cache test");
    SendRequest(fd, "/" + name + "?cnew=http&len=3");
    created = ReadResponse(fd);
    cid = ChannelId(created.headers);
    Cache mismatch(expected);
    Require(mismatch.Read(created.body) == 4, "Target-ID test channel creation failed");
    SendRequest(fd, "/jpip?cid=" + cid + full + "&tid=1&model=M0,M1,[0-]Hm,P0");
    Response response = ReadResponse(fd);
    Require(response.headers.find("200 OK") != string::npos &&
            response.headers.find("JPIP-tid: 0") != string::npos &&
            mismatch.Read(response.body) == 2 && mismatch.Complete(),
            "Mismatched target-ID model suppressed source bytes");
    SendRequest(fd, "/jpip?cclose=" + cid);
    Require(ReadResponse(fd).headers.find("200 OK") != string::npos, "Target-ID channel close failed");
    close(fd);
}

} // namespace jpp_test

void CheckJPPResponses(uint16_t port, const string &directory) {
    try {
        test_case = "JPP reader self-tests";
        jpp_test::CheckReader();
        jpp_test::Fixture fixture = jpp_test::MakeFixture(directory);
        for (bool linked : {false, true}) {
            if (linked) fixture.bins[jpp_test::Key(8, 0, 0)] = fixture.linked_metadata;
            for (bool gzip : {false, true})
                for (bool modeled : {false, true})
                    for (bool context : {false, true}) {
                        test_case = string("JPP ") + (linked ? "linked" : "embedded") +
                                (gzip ? "/gzip" : "/plain") +
                                (modeled ? "/partial-model" : "/empty-model") +
                                (context ? "/context" : "/stream");
                        jpp_test::Run(port, fixture.bins,
                                     linked ? "wire-linked.jpx" : "wire-embedded.jpx",
                                     gzip, modeled, context);
                    }
            test_case = string("JPP stateful/") + (linked ? "linked" : "embedded");
            jpp_test::Stateful(port, fixture.bins,
                              linked ? "wire-linked.jpx" : "wire-embedded.jpx");
        }
    } catch (const exception &error) { Fail(error.what()); }
    test_case.clear();
}
