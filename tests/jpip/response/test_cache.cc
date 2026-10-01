#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>

#include "jpip/response/cache_model.h"

using namespace std;

// Count requested bytes, independently of the cache's container layout.
// The aligned prefix lets delete account for each allocation without allocating.
struct alignas(max_align_t) AllocationHeader {
    size_t size;
};

static size_t allocated_bytes = 0;
static size_t allocation_calls = 0;
static size_t allocation_limit = SIZE_MAX;

void *operator new(size_t size) {
    if (size > allocation_limit - allocated_bytes ||
        size > SIZE_MAX - sizeof(AllocationHeader))
        throw bad_alloc();
    AllocationHeader *header = static_cast<AllocationHeader *>(
            malloc(sizeof(AllocationHeader) + size));
    if (header == NULL)
        throw bad_alloc();
    header->size = size;
    allocated_bytes += size;
    ++allocation_calls;
    return header + 1;
}

void *operator new[](size_t size) {
    return operator new(size);
}

void operator delete(void *p) noexcept {
    if (p != NULL) {
        AllocationHeader *header = static_cast<AllocationHeader *>(p) - 1;
        allocated_bytes -= header->size;
        free(header);
    }
}

void operator delete[](void *p) noexcept {
    operator delete(p);
}

struct AllocationLimit {
    size_t previous;

    explicit AllocationLimit(size_t budget) : previous(allocation_limit) {
        allocation_limit = budget > SIZE_MAX - allocated_bytes
                ? SIZE_MAX : allocated_bytes + budget;
    }

    ~AllocationLimit() {
        allocation_limit = previous;
    }
};

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static const jpip::DataBinClass classes[] = {
    jpip::DataBinClass::META_DATA,
    jpip::DataBinClass::MAIN_HEADER,
    jpip::DataBinClass::TILE_HEADER,
    jpip::DataBinClass::PRECINCT
};

static void CheckCacheModel() {
    for (jpip::DataBinClass bin_class : classes) {
        jpip::CacheModel model;
        const jpip::CacheModel &view = model;
        Check(view.GetDataBin(bin_class, 2, 3) == 0, "Nonempty initial cache model");
        Check(model.AddToDataBin(bin_class, 2, 3, 0) == 0 &&
                      view.GetDataBin(bin_class, 2, 3) == 0,
              "Zero increment changed a cache entry");
        Check(model.AddToDataBin(bin_class, 2, 3, 17) == 17 &&
                      view.GetDataBin(bin_class, 2, 3) == 17,
              "Wrong cache-model increment");
        Check(model.AugmentDataBin(bin_class, 2, 3, 23) == 23 &&
                      view.GetDataBin(bin_class, 2, 3) == 23,
              "Augmentation did not record the absolute cached length");
        Check(model.AugmentDataBin(bin_class, 2, 3, 7) == 23 &&
                      model.AugmentDataBin(bin_class, 2, 3, 23) == 23 &&
                      model.AddToDataBin(bin_class, 2, 3, 4) == 27 &&
                      view.GetDataBin(bin_class, 2, 3) == 27,
              "Repeated or smaller augmentation changed the cached length");
        Check(model.AddToDataBin(bin_class, 2, 3, 0, true) == INT_MAX &&
                      view.GetDataBin(bin_class, 2, 3) == INT_MAX,
              "Completion did not replace a partial cached length");
        Check(model.AddToDataBin(bin_class, 2, 3, 1) == INT_MAX &&
                      model.AugmentDataBin(bin_class, 2, 3, 29) == INT_MAX &&
                      view.GetDataBin(bin_class, 2, 3) == INT_MAX,
              "A completed bin became partial again");

        jpip::CacheModel saturated;
        Check(saturated.AddToDataBin(bin_class, 2, 3, 17) == 17 &&
                      saturated.AddToDataBin(bin_class, 2, 3, INT_MAX - 18) == INT_MAX - 1 &&
                      saturated.AddToDataBin(bin_class, 2, 3, 2) == INT_MAX &&
                      saturated.GetDataBin(bin_class, 2, 3) == INT_MAX,
              "Cache-model increment overflowed");
        jpip::CacheModel exact;
        Check(exact.AddToDataBin(bin_class, 2, 3, INT_MAX - 1) == INT_MAX - 1 &&
                      exact.AddToDataBin(bin_class, 2, 3, 1) == INT_MAX &&
                      exact.GetDataBin(bin_class, 2, 3) == INT_MAX,
              "Exact terminal-length increment failed");
        jpip::CacheModel flag_complete;
        Check(flag_complete.AddToDataBin(bin_class, 2, 3, 0, true) == INT_MAX &&
                      flag_complete.GetDataBin(bin_class, 2, 3) == INT_MAX,
              "Completion failed on an empty bin");
        jpip::CacheModel complete;
        Check(complete.AddToDataBin(bin_class, 2, 3, INT_MAX) == INT_MAX &&
                      complete.GetDataBin(bin_class, 2, 3) == INT_MAX,
              "Terminal-length increment failed on an empty bin");
        jpip::CacheModel augmented;
        Check(augmented.AugmentDataBin(bin_class, 2, 3, 17) == 17 &&
                      augmented.AugmentDataBin(bin_class, 2, 3, INT_MAX) == INT_MAX &&
                      augmented.GetDataBin(bin_class, 2, 3) == INT_MAX,
              "Terminal augmentation failed on a partial bin");
    }
}

static void CheckMetadata() {
    jpip::CacheModel model;
    const jpip::CacheModel &view = model;
    Check(!view.IsFullMetadata(), "Initial metadata state is complete");
    model.AddToDataBin(jpip::DataBinClass::META_DATA, 0, 1, 17);
    model.AddToDataBin(jpip::DataBinClass::META_DATA, 2, 3, 9);
    model.AddToDataBin(jpip::DataBinClass::MAIN_HEADER, 0, 0, 5);
    model.Pack();
    Check(view.GetDataBin(jpip::DataBinClass::META_DATA, 2, 1) == 17 &&
                  view.GetDataBin(jpip::DataBinClass::META_DATA, 0, 3) == 9 &&
                  view.GetDataBin(jpip::DataBinClass::META_DATA, 0, 2) == 0,
          "Metadata bins were merged or scoped to a codestream");
    size_t before = allocated_bytes;
    model.SetFullMetadata();
    Check(allocated_bytes < before, "Full metadata retained its entry storage");
    AllocationLimit no_allocation(0);
    for (int repeat = 0; repeat < 3; ++repeat) {
        model.SetFullMetadata();
        Check(view.IsFullMetadata() &&
                      view.GetDataBin(jpip::DataBinClass::META_DATA, 0, 1) == INT_MAX &&
                      view.GetDataBin(jpip::DataBinClass::META_DATA, 2, INT_MAX - 1) == INT_MAX &&
                      model.AddToDataBin(jpip::DataBinClass::META_DATA, 0, INT_MAX - 1, 7) == INT_MAX &&
                      model.AugmentDataBin(jpip::DataBinClass::META_DATA, 0, 1, 29) == INT_MAX &&
                      view.GetDataBin(jpip::DataBinClass::MAIN_HEADER, 0, 0) == 5,
              "Full metadata was changed, allocated storage, or altered a header");
    }
}

static void CheckSparsePacking() {
    jpip::CacheModel model;
    const jpip::CacheModel &view = model;
    using jpip::DataBinClass;
    model.AddToDataBin(DataBinClass::PRECINCT, 0, 0, 0, true);
    model.AddToDataBin(DataBinClass::PRECINCT, 0, 2, 0, true);
    model.AddToDataBin(DataBinClass::PRECINCT, 0, 3, 17);
    model.AddToDataBin(DataBinClass::PRECINCT, 1, 0, 9);
    model.AddToDataBin(DataBinClass::MAIN_HEADER, 0, 0, 5);
    model.AddToDataBin(DataBinClass::TILE_HEADER, 1, 0, 7);
    for (int repeat = 0; repeat < 3; ++repeat) {
        model.Pack();
        Check(view.GetDataBin(DataBinClass::PRECINCT, 0, 0) == INT_MAX &&
                      view.GetDataBin(DataBinClass::PRECINCT, 0, 1) == 0 &&
                      view.GetDataBin(DataBinClass::PRECINCT, 0, 2) == INT_MAX &&
                      view.GetDataBin(DataBinClass::PRECINCT, 0, 3) == 17 &&
                      view.GetDataBin(DataBinClass::PRECINCT, 0, 4) == 0,
              "Packing crossed a hole or changed its suffix");
        Check(view.GetDataBin(DataBinClass::PRECINCT, 1, 0) == 9 &&
                      view.GetDataBin(DataBinClass::MAIN_HEADER, 0, 0) == 5 &&
                      view.GetDataBin(DataBinClass::TILE_HEADER, 1, 0) == 7,
              "Packing changed another stream or header class");
    }
    model.AugmentDataBin(DataBinClass::PRECINCT, 0, 1, INT_MAX);
    model.Pack();
    Check(model.AddToDataBin(DataBinClass::PRECINCT, 0, 1, 3) == INT_MAX &&
                  model.AugmentDataBin(DataBinClass::PRECINCT, 0, 3, 7) == 17,
          "Updating a packed prefix or partial suffix changed cached bytes");
    model.AddToDataBin(DataBinClass::PRECINCT, 0, 3, 0, true);
    model.Pack();
    Check(view.GetDataBin(DataBinClass::PRECINCT, 0, 3) == INT_MAX &&
                  view.GetDataBin(DataBinClass::PRECINCT, 0, 4) == 0 &&
                  model.AddToDataBin(DataBinClass::PRECINCT, 0, 7, 11) == 11 &&
                  view.GetDataBin(DataBinClass::PRECINCT, 0, 6) == 0,
          "Emptying and regrowing packed storage lost its prefix or holes");
    for (DataBinClass cls : {DataBinClass::EXTENDED_PRECINCT, DataBinClass::TILE_DATA,
                            DataBinClass::EXTENDED_TILE})
        Check(model.GetDataBin(cls, 0, 0) == -1 &&
                      model.AddToDataBin(cls, 0, 0, 1) == -1 &&
                      model.AugmentDataBin(cls, 0, 0, 1) == -1,
              "An unsupported class changed cache state");
}

static void CheckReadOnly() {
    jpip::CacheModel model;
    AllocationLimit no_allocation(0);
    size_t calls = allocation_calls;
    for (jpip::DataBinClass bin_class : classes)
        Check(model.GetDataBin(bin_class, 99999, INT_MAX - 1) == 0 &&
                      model.AugmentDataBin(bin_class, 99999, INT_MAX - 1, 0) == 0,
              "Reading absent bins or a zero augmentation changed cache state");
    model.Pack();
    Check(allocation_calls == calls, "Empty cache reads or packing allocated storage");
}

static void CheckSparseHighId() {
    jpip::CacheModel model;
    const jpip::CacheModel &view = model;
    using jpip::DataBinClass;
    const int high_id = 931348323;
    AllocationLimit limit(128 * 1024);

    // Descending, widely separated ids exercise sparse insertion and growth.
    for (int i = 0; i < 256; ++i)
        Check(model.AugmentDataBin(DataBinClass::PRECINCT, i % 3,
                                  high_id - i * 100003, i + 1) == i + 1,
              "Did not accept a sparse high-id update");
    size_t calls = allocation_calls;
    {
        AllocationLimit no_allocation(0);
        for (int i = 0; i < 256; ++i) {
            int id = high_id - i * 100003;
            Check(view.GetDataBin(DataBinClass::PRECINCT, i % 3, id) == i + 1 &&
                          view.GetDataBin(DataBinClass::PRECINCT, i % 3, id - 1) == 0 &&
                          view.GetDataBin(DataBinClass::PRECINCT, (i + 1) % 3, id) == 0,
                  "Sparse growth lost entries or filled absent bins");
            Check(model.AugmentDataBin(DataBinClass::PRECINCT, i % 3, id, i + 1) == i + 1,
                  "Repeated sparse augmentation changed a bin");
        }
        Check(view.GetDataBin(DataBinClass::PRECINCT, 99999, high_id) == 0 &&
                      allocation_calls == calls,
              "Reading absent streams or bins allocated storage");
    }
    Check(model.AugmentDataBin(DataBinClass::PRECINCT, 0, high_id, INT_MAX) == INT_MAX,
          "Did not complete a sparse high-id precinct");
    model.AddToDataBin(DataBinClass::PRECINCT, 2, INT_MAX - 1, 11);
    model.Pack();
    Check(view.GetDataBin(DataBinClass::PRECINCT, 2, INT_MAX - 1) == 11 &&
                  view.GetDataBin(DataBinClass::PRECINCT, 0, high_id) == INT_MAX &&
                  view.GetDataBin(DataBinClass::PRECINCT, 0, high_id - 1) == 0,
          "Packing a high-id completion crossed an absent prefix");
}

static void CheckStorageRelease() {
    size_t initial = allocated_bytes;
    {
        jpip::CacheModel model;
        model.AddToDataBin(jpip::DataBinClass::MAIN_HEADER, 0, 0, 5);
        size_t baseline = allocated_bytes;
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (int i = 255; i >= 0; --i)
                model.AddToDataBin(jpip::DataBinClass::PRECINCT, 0,
                                   cycle * 256 + i, 0, true);
            Check(allocated_bytes > baseline, "Storage-release test populated no entries");
            model.Pack();
            Check(allocated_bytes == baseline,
                  "Packing an entirely complete prefix retained entry storage");
            Check(model.GetDataBin(jpip::DataBinClass::PRECINCT, 0, cycle * 256) == INT_MAX &&
                          model.GetDataBin(jpip::DataBinClass::PRECINCT, 0, (cycle + 1) * 256) == 0 &&
                          model.GetDataBin(jpip::DataBinClass::MAIN_HEADER, 0, 0) == 5,
                  "Releasing and regrowing storage lost cache state");
        }
    }
    Check(allocated_bytes == initial, "Cache destruction retained allocated storage");
}

static void CheckOperationSequence() {
    jpip::CacheModel model;
    // Logical lengths only: no packing, sparse containers, or prefix state.
    int expected[4][3][24] = {};
    bool full_metadata = false;
    uint32_t state = 0xCA43E;
    const int amounts[] = {0, 1, 7, 17, INT_MAX - 1, INT_MAX};
    for (int step = 0; step < 1200; ++step) {
        state = static_cast<uint32_t>(
                (static_cast<uint64_t>(state) * 1664525 + 1013904223) & UINT32_MAX);
        int cls = static_cast<int>((state >> 16) % 4);
        int stream = static_cast<int>((state >> 8) % 3);
        int id = static_cast<int>((state >> 3) % 24);
        int amount = amounts[(state >> 20) % 6];
        int &value = expected[cls][cls == 0 ? 0 : stream][cls == 1 || cls == 2 ? 0 : id];
        bool complete = step % 11 == 0;
        int result;
        if (step % 3 == 0) {
            result = model.AugmentDataBin(classes[cls], stream, id, amount);
            if (amount > value)
                value = amount;
        } else {
            result = model.AddToDataBin(classes[cls], stream, id, amount, complete);
            int64_t sum = static_cast<int64_t>(value) + amount;
            value = complete || sum >= INT_MAX ? INT_MAX : static_cast<int>(sum);
        }
        Check(result == value, "Cache update disagreed with logical reference lengths");
        if (step % 7 == 0)
            model.Pack();
        if (step == 900) {
            model.SetFullMetadata();
            full_metadata = true;
            for (int bin = 0; bin < 24; ++bin)
                expected[0][0][bin] = INT_MAX;
        }
        Check(model.IsFullMetadata() == full_metadata, "Wrong reference metadata state");
        for (int c = 0; c < 4; ++c)
            for (int s = 0; s < 3; ++s)
                for (int bin = 0; bin < 24; ++bin)
                    Check(model.GetDataBin(classes[c], s, bin) ==
                                  expected[c][c == 0 ? 0 : s][c == 1 || c == 2 ? 0 : bin],
                          "Cache state disagreed with logical reference lengths");
    }
}

int main() {
    try {
        CheckReadOnly();
        CheckSparseHighId();
        CheckSparsePacking();
        CheckCacheModel();
        CheckMetadata();
        CheckStorageRelease();
        CheckOperationSequence();
    } catch (const bad_alloc &) {
        Check(false, "Cache operation exceeded its allocation budget");
    }
    return EXIT_SUCCESS;
}
