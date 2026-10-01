#include "jpip_support.h"
#include <climits>
#include "jpip/response/cache_model.h"

extern "C" int replay_jpip_cache(const uint8_t *data, size_t size) {
    using namespace jpip_fuzz;
    const jpip::DataBinClass classes[] = {jpip::META_DATA, jpip::MAIN_HEADER,
        jpip::TILE_HEADER, jpip::PRECINCT, jpip::EXTENDED_PRECINCT,
        jpip::TILE_DATA, jpip::EXTENDED_TILE};
    const int amounts[] = {0, 1, 7, 127, 128, INT_MAX - 1, INT_MAX};
    Input input(data, size);
    jpip::CacheModel cache;
    std::map<Key, int> expected;
    bool full_metadata = false;
    auto lookup = [&](const Key &key) {
        if (std::get<0>(key) == jpip::META_DATA && full_metadata) return INT_MAX;
        auto found = expected.find(key);
        return found == expected.end() ? 0 : found->second;
    };
    for (int step = 0; input.more() && step < 128; ++step) {
        unsigned op = input.byte() % 6;
        jpip::DataBinClass cls = classes[input.byte() % 7];
        int stream = input.byte() % 4;
        unsigned selector = input.byte();
        int id = cls == jpip::PRECINCT && (selector & 128)
                ? INT_MAX - 1 - (selector % 32) : selector % 32;
        int amount = amounts[input.byte() % 7];
        bool last = input.byte() & 1;
        Key key(cls, cls == jpip::META_DATA ? 0 : stream,
                cls == jpip::MAIN_HEADER || cls == jpip::TILE_HEADER ? 0 : id);
        bool supported = cls == jpip::META_DATA || cls == jpip::MAIN_HEADER ||
                         cls == jpip::TILE_HEADER || cls == jpip::PRECINCT;
        int old = supported ? lookup(key) : -1, result = old;
        if (op == 0) {
            cache.Pack();
        } else if (op == 1) {
            cache.SetFullMetadata();
            full_metadata = true;
        } else if (op == 2) {
            result = cache.GetDataBin(cls, stream, id);
        } else if (op == 3) {
            result = cache.AugmentDataBin(cls, stream, id, amount);
            if (supported) expected[key] = std::max(old, amount);
        } else {
            result = cache.AddToDataBin(cls, stream, id, amount, last);
            if (supported) {
                int64_t sum = static_cast<int64_t>(old) + amount;
                expected[key] = last || sum >= INT_MAX ? INT_MAX : static_cast<int>(sum);
            }
        }
        if (op >= 2)
            require(result == (supported ? lookup(key) : -1), "cache update disagrees with reference");
        require(cache.IsFullMetadata() == full_metadata, "wrong full-metadata state");
        for (const auto &entry : expected)
            require(cache.GetDataBin(static_cast<jpip::DataBinClass>(std::get<0>(entry.first)),
                        static_cast<int>(std::get<1>(entry.first)),
                        static_cast<int>(std::get<2>(entry.first))) == lookup(entry.first),
                    "packing or update changed another bin");
        require(cache.GetDataBin(jpip::PRECINCT, 0, 64) == 0,
                "packing filled an untouched precinct");
    }
    return 0;
}

#ifndef ESAJPIP_FUZZ_REPLAY
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    return replay_jpip_cache(data, size);
}
#endif
