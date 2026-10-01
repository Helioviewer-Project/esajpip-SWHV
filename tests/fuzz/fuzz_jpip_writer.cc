#include "jpip_support.h"
#include "jpip/response/databin_writer.h"

extern "C" int replay_jpip_writer(const uint8_t *data, size_t size) {
    using namespace jpip_fuzz;
    Input input(data, size);
    const int capacities[] = {0, 1, 2, 3, 6, 7, 22, 60, 61, 127, 128, 129, 255, 256, 1024, 16410, 32784};
    const unsigned lengths[] = {0, 1, 15, 16, 126, 127, 128, 129, 16382, 16383, 16384};
    unsigned capacity_selector = input.byte();
    int capacity = capacity_selector & 128 ? static_cast<int>(input.number(2) % 33000)
                                          : capacities[capacity_selector % 17];
    Buffer buffer(capacity);
    Bytes source_bytes(16416);
    for (size_t i = 0; i < source_bytes.size(); ++i)
        source_bytes[i] = static_cast<uint8_t>((i * 31) % 256);
    jpip::Source source(source_bytes.data(), source_bytes.size());
    jpip::DataBinWriter writer;
    writer.SetBuffer(buffer.data(), buffer.capacity);
    writer.StartResponse();
    Wire wire;
    std::vector<Message> expected;
    bool merge = false;
    auto flush = [&](int eor = -1) {
        int length = static_cast<int>(writer.Finalize());
        buffer.check(length);
        require(writer.Finalize() == length, "writer finalization is not idempotent");
        Bytes bytes(buffer.wire_bytes(), buffer.wire_bytes() + length);
        int reason;
        std::vector<Message> actual = wire.decode(bytes, &reason);
        require(reason == eor && actual.size() == expected.size(), "wrong EOR or message count");
        for (size_t i = 0; i < actual.size(); ++i)
            require(actual[i].key == expected[i].key && actual[i].offset == expected[i].offset &&
                    actual[i].payload == expected[i].payload && actual[i].last == expected[i].last,
                    "written header or payload differs from contributions");
        expected.clear();
        merge = false;
        writer.SetBuffer(buffer.data(), buffer.capacity);
    };
    for (unsigned op = 0; input.more() && op < 32; ++op) {
        unsigned flags = input.byte();
        if ((flags & 7) == 0) {
            flush();
            if (flags & 8) { writer.StartResponse(); wire = Wire(); }
            continue;
        }
        const int classes[] = {jpip::PRECINCT, jpip::TILE_HEADER, jpip::MAIN_HEADER, jpip::META_DATA};
        int cls = classes[input.byte() % 4];
        int stream = input.byte();
        uint64_t bin = input.boundary(), offset = input.boundary();
        if ((flags & 16) && !expected.empty()) {
            const Message &previous = expected.back();
            cls = static_cast<int>(std::get<0>(previous.key));
            stream = static_cast<int>(std::get<1>(previous.key));
            bin = std::get<2>(previous.key);
            if (previous.payload.size() <= UINT64_MAX - previous.offset)
                offset = previous.offset + previous.payload.size();
        }
        bool last = flags & 32;
        unsigned length = lengths[input.byte() % 11], start = input.byte() % 32;
        Bytes payload(source_bytes.begin() + start, source_bytes.begin() + start + length);
        jpip::DataBinWriter::Result result;
        if (flags & 64) {
            // Independently form both kinds of placeholder and their slices.
            jpip::PlaceHolder placeholder(input.byte(), input.byte() & 1,
                                          jpip::FileSegment(start, (input.byte() % 3) * 8));
            Bytes encoded;
            put(encoded, 20 + placeholder.header.length + (placeholder.is_jp2c ? 24 : 0), 4);
            put(encoded, 0x70686c64, 4);
            put(encoded, placeholder.is_jp2c ? 4 : 1, 4);
            put(encoded, placeholder.is_jp2c ? 0 : placeholder.id, 8);
            encoded.insert(encoded.end(), source_bytes.begin() + start,
                           source_bytes.begin() + start + placeholder.header.length);
            if (placeholder.is_jp2c) { put(encoded, 0, 8); put(encoded, 0, 8); put(encoded, placeholder.id, 8); }
            uint64_t skip = input.byte() % (encoded.size() + 2);
            // The caller supplies an offset whose sum with skip is representable.
            offset = std::min(offset, UINT64_MAX - skip);
            result = writer.WritePlaceHolder(cls, stream, bin, offset, source, placeholder, skip, last, length);
            if (skip > encoded.size()) {
                require(result == jpip::DataBinWriter::Result::FAILED, "invalid placeholder skip accepted");
                buffer.check(buffer.capacity - static_cast<int>(writer.GetFree()));
                continue;
            }
            uint64_t count = std::min(uint64_t(length), encoded.size() - skip);
            payload.assign(encoded.begin() + skip, encoded.begin() + skip + count);
            offset += skip;
            last = last && count == encoded.size() - skip;
        } else {
            result = writer.Write(cls, stream, bin, offset, source,
                                  jpip::FileSegment(start, length), last);
        }
        if (result == jpip::DataBinWriter::Result::WRITTEN) {
            Key key(cls, stream, bin);
            if (merge && !expected.empty() && !expected.back().last && expected.back().key == key &&
                expected.back().payload.size() <= UINT64_MAX - expected.back().offset &&
                expected.back().offset + expected.back().payload.size() == offset) {
                expected.back().payload.insert(expected.back().payload.end(), payload.begin(), payload.end());
                expected.back().last = last;
            } else {
                expected.push_back({key, offset, payload, last});
            }
            merge = true;
        } else {
            require(result == jpip::DataBinWriter::Result::FULL, "valid source contribution failed");
            flush();
        }
        // Do not finalize here: keep coalescing and header growth reachable.
        buffer.check(buffer.capacity - static_cast<int>(writer.GetFree()));
    }
    bool eor = writer.WriteEOR(jpip::EOR::WINDOW_DONE);
    flush(eor ? jpip::EOR::WINDOW_DONE : -1);
    return 0;
}

#ifndef ESAJPIP_FUZZ_REPLAY
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    return replay_jpip_writer(data, size);
}
#endif
