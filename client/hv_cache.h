/* hv_cache.h: the client's store of delivered data-bins.
 *
 * One channel owns one store. Each data-bin is identified by its class,
 * codestream and Bin-ID, and arrives in messages that must append without a gap,
 * because the server tracks exactly the same prefixes: if the client and server
 * disagree about a bin's length, every later request is wrong. The store
 * therefore refuses any message that does not continue a bin where it left off,
 * rather than trying to reconcile the difference.
 *
 * A bin is complete once a message sets the last-byte flag. Bins stay in
 * memory for the life of the channel, complete or not: the server will not
 * resend what it has sent, and it takes no notice that a client has dropped
 * something (JPIP_PROFILE.md: no subtractive cache model).
 *
 * The store is a hash table on a bin's identity, so it holds the many
 * codestreams of a movie; memory is its only limit.
 */
#ifndef HV_CACHE_H
#define HV_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include "hv_jpp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One delivered data-bin. A bin grows by reallocation as later layers arrive. */
struct hv_frame;
typedef struct {
    int      used;         /* this slot of the table holds a bin */
    int      bin_class;
    uint64_t codestream;
    uint64_t bin_id;
    uint8_t *data;
    size_t   length;
    size_t   capacity;
    int      complete;
    struct hv_frame *frame; /* Prepared information for a main-header bin. */
} hv_bin;

typedef struct {
    hv_bin *bins;         /* the table: `capacity` slots, a power of two */
    size_t  count;        /* bins held */
    size_t  capacity;
    size_t  bytes;        /* total payload held */
    const char *error;   /* NULL unless the last hv_cache_apply failed */
} hv_cache;

/* Prepares an empty store. */
void hv_cache_begin(hv_cache *cache);

/* Applies one message. Returns 0 and sets hv_cache_error() when the message
 * does not continue the bin the server believes it does, when memory runs out,
 * or when a message arrives for an already completed bin. A rejected message
 * leaves the store unchanged. */
int hv_cache_apply(hv_cache *cache, const hv_jpp_message *message);

/* Looks a bin up, or NULL when the client has not received any of it. The
 * pointer is good until the next hv_cache_apply(). */
const hv_bin *hv_cache_find(const hv_cache *cache, int bin_class,
                             uint64_t codestream, uint64_t bin_id);

/* True when the named bin has been delivered in full. */
int hv_cache_complete(const hv_cache *cache, int bin_class, uint64_t codestream,
                      uint64_t bin_id);

/* How many bytes of a bin the client holds, which is what the server's cache
 * model should report for it. */
size_t hv_cache_length(const hv_cache *cache, int bin_class, uint64_t codestream,
                       uint64_t bin_id);

/* Counts of each kind held, for a status line in the host. */
size_t hv_cache_bin_count(const hv_cache *cache);
size_t hv_cache_total_bytes(const hv_cache *cache);
size_t hv_cache_complete_count(const hv_cache *cache);

/* A nonempty diagnostic after a failed hv_cache_apply(). */
const char *hv_cache_error(const hv_cache *cache);

/* Releases every bin. Safe on an already-released store. */
void hv_cache_release(hv_cache *cache);

#ifdef __cplusplus
}
#endif
#endif /* HV_CACHE_H */
