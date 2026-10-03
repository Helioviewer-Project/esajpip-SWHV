/* hv_cache.c: see hv_cache.h. */
#include "hv_cache.h"
#include "hv_frame.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>

static void Fail(hv_cache *cache, const char *message) {
    cache->error = message;
}

void hv_cache_begin(hv_cache *cache) {
    cache->bins = NULL;
    cache->count = 0;
    cache->capacity = 0;
    cache->bytes = 0;
    cache->error = NULL;
}

/* Unsigned arithmetic that wraps on purpose, which Clang's
 * -fsanitize=integer would report. */
#if defined(__clang__)
#define WRAPS __attribute__((no_sanitize("unsigned-integer-overflow", "unsigned-shift-base")))
#else
#define WRAPS
#endif

/* The table is open-addressed: a bin sits at the first free slot from the one
 * its identity hashes to, and the table is at most half full. */
WRAPS static size_t Slot(const hv_bin *bins, size_t capacity, int bin_class,
                         uint64_t codestream, uint64_t bin_id) {
    uint64_t hash = (codestream * 8 + (uint64_t) bin_class) * UINT64_C(0x9E3779B97F4A7C15) ^
                    bin_id;
    size_t index;
    hash ^= hash >> 31;
    hash *= UINT64_C(0xBF58476D1CE4E5B9);
    hash ^= hash >> 29;
    for (index = (size_t) hash & (capacity - 1); bins[index].used;
         index = (index + 1) & (capacity - 1)) {
        if (bins[index].bin_class == bin_class && bins[index].codestream == codestream &&
            bins[index].bin_id == bin_id)
            break;
    }
    return index;
}

const hv_bin *hv_cache_find(const hv_cache *cache, int bin_class,
                             uint64_t codestream, uint64_t bin_id) {
    const hv_bin *bin;
    if (cache->capacity == 0) return NULL;
    bin = &cache->bins[Slot(cache->bins, cache->capacity, bin_class, codestream, bin_id)];
    return bin->used ? bin : NULL;
}

/* Makes room for one more bin. A failed growth leaves the table as it was. */
static int Grow(hv_cache *cache) {
    size_t capacity = cache->capacity ? cache->capacity * 2 : 64;
    size_t index;
    hv_bin *bins;
    if (2 * (cache->count + 1) <= cache->capacity) return 0;
    if (capacity > (size_t) -1 / sizeof *bins) return -1;
    bins = (hv_bin *) calloc(capacity, sizeof *bins);
    if (bins == NULL) return -1;
    for (index = 0; index < cache->capacity; index++) {
        const hv_bin *bin = &cache->bins[index];
        if (bin->used)
            bins[Slot(bins, capacity, bin->bin_class, bin->codestream, bin->bin_id)] = *bin;
    }
    free(cache->bins);
    cache->bins = bins;
    cache->capacity = capacity;
    return 0;
}

/* Grows the bin's buffer for one more message. A failed growth leaves what was
 * already received. */
static int Reserve(hv_bin *bin, size_t needed) {
    size_t capacity;
    uint8_t *grown;
    if (needed <= bin->capacity) return 0;
    capacity = bin->capacity ? bin->capacity : 256;
    while (capacity < needed) {
        if (capacity > (size_t) -1 / 2) return -1;
        capacity *= 2;
    }
    grown = (uint8_t *) realloc(bin->data, capacity);
    if (grown == NULL) return -1;
    bin->data = grown;
    bin->capacity = capacity;
    return 0;
}

int hv_cache_apply(hv_cache *cache, const hv_jpp_message *message) {
    hv_bin fresh = {0};
    hv_bin *bin = (hv_bin *) hv_cache_find(cache, message->bin_class, message->codestream,
                                            message->bin_id);
    size_t needed;

    cache->error = NULL;

    if (message->length > (size_t) -1 - (size_t) message->offset) {
        Fail(cache, "Data-bin range overflows");
        return 0;
    }
    needed = (size_t) message->offset + (size_t) message->length;

    /* A message must continue the bin where the previous one stopped. Anything
     * else means the server's record of what this client holds has diverged from
     * the client's, and no later request can be trusted to correct it. */
    if (message->offset != (bin != NULL ? bin->length : 0)) {
        Fail(cache, "Message does not continue the data-bin the client holds");
        return 0;
    }
    if (bin != NULL && bin->complete) {
        Fail(cache, "Message extends a data-bin the server already completed");
        return 0;
    }
    if (bin == NULL) {
        if (Grow(cache) != 0) {
            Fail(cache, "Out of memory for the data-bin table");
            return 0;
        }
        fresh.used = 1;
        fresh.bin_class = message->bin_class;
        fresh.codestream = message->codestream;
        fresh.bin_id = message->bin_id;
    }
    if (Reserve(bin != NULL ? bin : &fresh, needed) != 0) {
        Fail(cache, "Out of memory for a data-bin");
        return 0;
    }
    if (bin == NULL) {
        bin = &cache->bins[Slot(cache->bins, cache->capacity, fresh.bin_class,
                                fresh.codestream, fresh.bin_id)];
        *bin = fresh;
        cache->count++;
    }
    if (message->length) {
        memcpy(bin->data + bin->length, message->data, (size_t) message->length);
        bin->length = needed;
        cache->bytes += (size_t) message->length;
    }
    if (message->last_byte) bin->complete = 1;
    return 1;
}

int hv_cache_complete(const hv_cache *cache, int bin_class, uint64_t codestream,
                      uint64_t bin_id) {
    const hv_bin *bin = hv_cache_find(cache, bin_class, codestream, bin_id);
    return bin != NULL && bin->complete;
}

size_t hv_cache_length(const hv_cache *cache, int bin_class, uint64_t codestream,
                       uint64_t bin_id) {
    const hv_bin *bin = hv_cache_find(cache, bin_class, codestream, bin_id);
    return bin != NULL ? bin->length : 0;
}

size_t hv_cache_bin_count(const hv_cache *cache) {
    return cache->count;
}

size_t hv_cache_total_bytes(const hv_cache *cache) {
    return cache->bytes;
}

size_t hv_cache_complete_count(const hv_cache *cache) {
    size_t index;
    size_t complete = 0;
    for (index = 0; index < cache->capacity; index++) {
        if (cache->bins[index].used && cache->bins[index].complete) complete++;
    }
    return complete;
}

const char *hv_cache_error(const hv_cache *cache) {
    return cache->error != NULL ? cache->error : "";
}

int hv_cache_model(const hv_cache *cache, size_t *cursor, char *text, size_t size) {
    size_t used = 0;
    if (size == 0) return -1;
    text[0] = '\0';
    while (*cursor < cache->capacity) {
        const hv_bin *bin = &cache->bins[*cursor];
        char descriptor[128];
        int length;
        const char *kind;
        if (!bin->used || (!bin->complete && bin->length == 0)) {
            ++*cursor;
            continue;
        }
        switch (bin->bin_class) {
        case HV_BIN_META_DATA: kind = "M"; break;
        case HV_BIN_MAIN_HEADER: kind = "Hm"; break;
        case HV_BIN_TILE_HEADER: kind = "H"; break;
        case HV_BIN_PRECINCT: kind = "P"; break;
        default: return -1;
        }
        if ((bin->bin_class == HV_BIN_META_DATA && !bin->complete) ||
            (!bin->complete && bin->length >= INT_MAX) || bin->bin_id > INT_MAX ||
            ((bin->bin_class == HV_BIN_MAIN_HEADER || bin->bin_class == HV_BIN_TILE_HEADER) &&
             bin->bin_id != 0)) return -1;
        if (bin->bin_class == HV_BIN_META_DATA)
            length = snprintf(descriptor, sizeof descriptor, "M%" PRIu64, bin->bin_id);
        else if (bin->bin_class == HV_BIN_MAIN_HEADER)
            length = snprintf(descriptor, sizeof descriptor, "[%" PRIu64 "]Hm", bin->codestream);
        else
            length = snprintf(descriptor, sizeof descriptor, "[%" PRIu64 "]%s%" PRIu64,
                              bin->codestream, kind, bin->bin_id);
        if (!bin->complete)
            length += snprintf(descriptor + length, sizeof descriptor - (size_t)length,
                               ":%zu", bin->length);
        if (length < 0 || (size_t)length >= sizeof descriptor) return -1;
        if (used + (used != 0) + (size_t)length >= size)
            return used ? (int)used : -1;
        if (used) text[used++] = ',';
        memcpy(text + used, descriptor, (size_t)length + 1);
        used += (size_t)length;
        ++*cursor;
    }
    return (int)used;
}

int hv_cache_match_metadata(const hv_cache *cache, const hv_jpp_message *message) {
    const hv_bin *bin;
    if (message->bin_class != HV_BIN_META_DATA) return 0;
    bin = hv_cache_find(cache, HV_BIN_META_DATA, message->codestream, message->bin_id);
    if (bin == NULL || !bin->complete || message->offset > bin->length ||
        message->length > bin->length - (size_t)message->offset ||
        (message->last_byte && message->offset + message->length != bin->length)) return 0;
    return message->length == 0 ||
           memcmp(bin->data + (size_t)message->offset, message->data,
                  (size_t)message->length) == 0;
}

void hv_cache_release(hv_cache *cache) {
    size_t index;
    for (index = 0; index < cache->capacity; index++) {
        hv_frame_free(cache->bins[index].frame);
        free(cache->bins[index].data);
    }
    free(cache->bins);
    cache->bins = NULL;
    cache->count = 0;
    cache->capacity = 0;
    cache->bytes = 0;
    cache->error = NULL;
}
