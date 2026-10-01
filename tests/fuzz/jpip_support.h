#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace jpip_fuzz {

using Bytes = std::vector<uint8_t>;
using Key = std::tuple<uint64_t, uint64_t, uint64_t>;

inline void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "JPIP fuzz invariant: %s\n", message);
        std::abort();
    }
}

// Missing fields are zero. Operation counts are bounded by each target.
struct Input {
    const uint8_t *data;
    size_t size, at = 0;
    Input(const uint8_t *_data, size_t _size) : data(_data), size(_size) {}
    bool more() const { return at < size; }
    uint8_t byte() { return more() ? data[at++] : 0; }
    uint64_t number(unsigned width = 8) {
        uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i)
            value = value * 256 + byte();
        return value;
    }
    uint64_t boundary() {
        static const uint64_t values[] = {
            0, 1, 15, 16, 127, 128, 129, 2047, 2048, 16383, 16384,
            UINT32_MAX, uint64_t(UINT32_MAX) + 1, UINT64_MAX - 1, UINT64_MAX
        };
        unsigned choice = byte();
        return choice & 128 ? number() : values[choice % (sizeof values / sizeof *values)];
    }
};

inline void put(Bytes &bytes, uint64_t value, unsigned width) {
    while (width) {
        --width;
        bytes.push_back(static_cast<uint8_t>(value >> (width * 8)));
    }
}

struct Buffer {
    std::vector<char> bytes;
    int capacity;
    explicit Buffer(int _capacity) : bytes(_capacity + 32, 0x5a), capacity(_capacity) {}
    char *data() { return bytes.data() + 16; }
    const uint8_t *wire_bytes() const {
        return reinterpret_cast<const uint8_t *>(bytes.data() + 16);
    }
    void check(int used) const {
        require(used >= 0 && used <= capacity, "output length outside buffer");
        for (int i = 0; i < 16; ++i)
            require(bytes[i] == 0x5a && bytes[16 + capacity + i] == 0x5a,
                    "output buffer guard overwritten");
    }
};

struct Message {
    Key key;
    uint64_t offset;
    Bytes payload;
    bool last;
};

// Independent JPP decoder. Does not use the production integer/header writer.
struct Wire {
    uint64_t cls = 0, stream = 0;
    bool has_class = false, has_stream = false;
    static uint64_t integer(const Bytes &bytes, size_t &at, uint64_t value = 0,
                            bool continuation = true) {
        unsigned count = 0;
        while (continuation) {
            require(at < bytes.size() && ++count <= 10, "truncated or oversized JPP integer");
            uint8_t byte = bytes[at++];
            uint64_t digit = byte % 128;
            require(count != 1 || byte != 128 || value != 0, "noncanonical JPP integer");
            require(value <= (UINT64_MAX - digit) / 128, "JPP integer overflow");
            value = value * 128 + digit;
            continuation = (byte & 128) != 0;
        }
        return value;
    }
    std::vector<Message> decode(const Bytes &bytes, int *reason = NULL) {
        std::vector<Message> messages;
        if (reason) *reason = -1;
        size_t at = 0;
        while (at < bytes.size()) {
            uint8_t first = bytes[at++];
            if (first == 0) {
                require(reason != NULL && at < bytes.size(), "unexpected EOR");
                *reason = bytes[at++];
                uint64_t length = integer(bytes, at);
                require(length == 0 && at == bytes.size(), "invalid EOR payload or suffix");
                break;
            }
            unsigned presence = (first >> 5) & 3;
            require(presence != 0, "invalid JPP presence bits");
            uint64_t id = first & 15;
            if (first & 128) {
                id = integer(bytes, at, id);
                require(id >= 16, "noncanonical Bin-ID");
            }
            if (presence >= 2) { cls = integer(bytes, at); has_class = true; }
            if (presence == 3) { stream = integer(bytes, at); has_stream = true; }
            require(has_class && has_stream, "missing initial JPP identifiers");
            uint64_t offset = integer(bytes, at), length = integer(bytes, at);
            require(length <= bytes.size() - at, "truncated JPP payload");
            Message message{Key(cls, stream, id), offset,
                            Bytes(bytes.begin() + at, bytes.begin() + at + length),
                            (first & 16) != 0};
            messages.push_back(std::move(message));
            at += static_cast<size_t>(length);
        }
        return messages;
    }
};

struct Received {
    std::map<Key, Bytes> bins;
    std::map<Key, bool> complete;
    int decode(const Bytes &response) {
        Wire wire;
        int reason;
        for (const Message &message : wire.decode(response, &reason)) {
            Bytes &bin = bins[message.key];
            require(message.offset == bin.size(), "duplicate or missing data-bin bytes");
            require(!complete[message.key], "message after complete bin");
            require(!message.payload.empty() || message.last, "empty non-final contribution");
            bin.insert(bin.end(), message.payload.begin(), message.payload.end());
            complete[message.key] = message.last;
        }
        require(reason >= 0, "response has no EOR");
        return reason;
    }
};

} // namespace jpip_fuzz
