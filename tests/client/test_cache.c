/* test_cache.c: checks the client's data-bin store.
 *
 * The store's central rule is that a message must continue its bin exactly where
 * the previous one stopped, because the server keeps the same record of what the
 * client holds. A gap means the two disagree, and the only safe response is to
 * drop the channel rather than try to reconcile it. These cases cover that rule,
 * completion, and the bounds the store enforces.
 */
#include "hv_cache.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void Check(int condition, const char *message) {
    if (!condition) {
        printf("FAIL %s\n", message);
        failures++;
    }
}

/* Builds a message delivering length bytes of the given payload at an offset. */
static hv_jpp_message Message(int cls, uint64_t codestream, uint64_t bin_id,
                              uint64_t offset, const char *payload,
                              size_t length, int last) {
    hv_jpp_message message;
    message.bin_class = cls;
    message.codestream = codestream;
    message.bin_id = bin_id;
    message.offset = offset;
    message.length = length;
    message.last_byte = last;
    message.data = (const uint8_t *) payload;
    return message;
}

static void TestContiguousAppend(void) {
    hv_cache cache;
    hv_jpp_message message;

    hv_cache_begin(&cache);
    message = Message(HV_BIN_PRECINCT, 0, 5, 0, "abc", 3, 0);
    Check(hv_cache_apply(&cache, &message), "first message accepted");
    message = Message(HV_BIN_PRECINCT, 0, 5, 3, "def", 3, 0);
    Check(hv_cache_apply(&cache, &message), "continuing message accepted");
    message = Message(HV_BIN_PRECINCT, 0, 5, 6, "gh", 2, 1);
    Check(hv_cache_apply(&cache, &message), "final message accepted");

    Check(hv_cache_bin_count(&cache) == 1, "one bin after three messages");
    Check(hv_cache_length(&cache, HV_BIN_PRECINCT, 0, 5) == 8, "bin length");
    Check(hv_cache_complete(&cache, HV_BIN_PRECINCT, 0, 5), "bin is complete");
    Check(hv_cache_total_bytes(&cache) == 8, "total bytes");
    {
        const hv_bin *bin = hv_cache_find(&cache, HV_BIN_PRECINCT, 0, 5);
        Check(bin != NULL && memcmp(bin->data, "abcdefgh", 8) == 0,
              "bin payload in order");
    }
    hv_cache_release(&cache);
}

static void TestGapsRejected(void) {
    hv_cache cache;
    hv_jpp_message message;

    /* A message that skips ahead of the recorded length would leave the client
     * and server disagreeing about the bin. */
    hv_cache_begin(&cache);
    message = Message(HV_BIN_PRECINCT, 0, 1, 0, "abc", 3, 0);
    Check(hv_cache_apply(&cache, &message), "seed accepted");
    message = Message(HV_BIN_PRECINCT, 0, 1, 5, "xyz", 3, 0);
    Check(!hv_cache_apply(&cache, &message), "gap rejected");
    Check(hv_cache_error(&cache)[0] != 0, "gap has a diagnostic");

    /* A retransmission of bytes already held is the same disagreement. */
    message = Message(HV_BIN_PRECINCT, 0, 1, 0, "abc", 3, 0);
    Check(!hv_cache_apply(&cache, &message), "retransmission rejected");

    /* The rejected messages must not have changed the store. */
    Check(hv_cache_length(&cache, HV_BIN_PRECINCT, 0, 1) == 3,
          "store unchanged after rejection");

    /* A completed bin takes no further bytes. */
    message = Message(HV_BIN_PRECINCT, 0, 1, 3, "d", 1, 1);
    Check(hv_cache_apply(&cache, &message), "bin completes");
    message = Message(HV_BIN_PRECINCT, 0, 1, 4, "e", 1, 0);
    Check(!hv_cache_apply(&cache, &message), "message after completion rejected");
    hv_cache_release(&cache);
}

static void TestDistinctBins(void) {
    hv_cache cache;
    hv_jpp_message message;

    /* Identity is class, codestream and Bin-ID together, so the same id under a
     * different codestream is a different bin. */
    hv_cache_begin(&cache);
    message = Message(HV_BIN_MAIN_HEADER, 0, 0, 0, "hdr", 3, 1);
    Check(hv_cache_apply(&cache, &message), "main header accepted");
    message = Message(HV_BIN_MAIN_HEADER, 1, 0, 0, "hdr2", 4, 1);
    Check(hv_cache_apply(&cache, &message), "second codestream main header");
    message = Message(HV_BIN_TILE_HEADER, 0, 0, 0, "", 0, 1);
    Check(hv_cache_apply(&cache, &message), "empty tile header accepted");

    Check(hv_cache_bin_count(&cache) == 3, "three distinct bins");
    Check(hv_cache_length(&cache, HV_BIN_MAIN_HEADER, 0, 0) == 3,
          "codestream 0 header length");
    Check(hv_cache_length(&cache, HV_BIN_MAIN_HEADER, 1, 0) == 4,
          "codestream 1 header length");
    Check(hv_cache_complete(&cache, HV_BIN_TILE_HEADER, 0, 0),
          "empty tile header is complete");
    Check(hv_cache_complete_count(&cache) == 3, "all bins complete");
    hv_cache_release(&cache);
}

static void TestUnknownBinIsZero(void) {
    hv_cache cache;

    hv_cache_begin(&cache);
    Check(hv_cache_find(&cache, HV_BIN_PRECINCT, 0, 99) == NULL, "unknown bin absent");
    Check(hv_cache_length(&cache, HV_BIN_PRECINCT, 0, 99) == 0, "unknown length zero");
    Check(!hv_cache_complete(&cache, HV_BIN_PRECINCT, 0, 99), "unknown not complete");
    Check(hv_cache_error(&cache)[0] == 0, "no diagnostic without a failure");
    hv_cache_release(&cache);
}

/* A precinct bin can arrive over many quality layers, so the store must grow
 * without losing earlier layers. */
static void TestGrowthKeepsContents(void) {
    hv_cache cache;
    hv_jpp_message message;
    char payload[512];
    size_t layer;
    size_t total = 0;

    hv_cache_begin(&cache);
    for (layer = 0; layer < 40; layer++) {
        memset(payload, (int) ('a' + (layer % 26)), sizeof payload);
        message = Message(HV_BIN_PRECINCT, 0, 0, total, payload, 300,
                          layer == 39);
        Check(hv_cache_apply(&cache, &message), "layer accepted");
        total += 300;
    }
    Check(hv_cache_length(&cache, HV_BIN_PRECINCT, 0, 0) == total, "grown bin length");
    {
        const hv_bin *bin = hv_cache_find(&cache, HV_BIN_PRECINCT, 0, 0);
        int intact = bin != NULL;
        size_t index;
        for (index = 0; intact && index < total; index++) {
            if (bin->data[index] != (uint8_t) ('a' + ((index / 300) % 26)))
                intact = 0;
        }
        Check(intact, "every layer survived growth in order");
    }
    hv_cache_release(&cache);
}

/* A length that cannot be satisfied must be reported rather than attempted. */
static void TestHostileLength(void) {
    hv_cache cache;
    hv_jpp_message message;

    hv_cache_begin(&cache);
    message = Message(HV_BIN_PRECINCT, 0, 0, (uint64_t) -1 - 4, "ab", 8, 0);
    Check(!hv_cache_apply(&cache, &message), "overflowing range rejected");
    Check(hv_cache_error(&cache)[0] != 0, "overflow has a diagnostic");
    hv_cache_release(&cache);
}

/* A movie's worth of bins: every one stays findable, with its own bytes, as
 * the table grows. */
static void TestManyBins(void) {
    enum { CODESTREAMS = 40, BINS = 1500 };
    hv_cache cache;
    hv_jpp_message message;
    uint64_t codestream, bin_id;
    int wrong = 0;

    hv_cache_begin(&cache);
    for (codestream = 0; codestream < CODESTREAMS; codestream++) {
        for (bin_id = 0; bin_id < BINS; bin_id++) {
            uint8_t payload[2] = {(uint8_t) codestream, (uint8_t) bin_id};
            message = Message(HV_BIN_PRECINCT, codestream, bin_id, 0, (const char *) payload, 2,
                              bin_id % 2);
            wrong += !hv_cache_apply(&cache, &message);
        }
    }
    Check(!wrong, "many bins accepted");
    Check(hv_cache_bin_count(&cache) == CODESTREAMS * BINS, "many bins counted");
    Check(hv_cache_total_bytes(&cache) == 2 * CODESTREAMS * BINS, "many bins' bytes counted");
    Check(hv_cache_complete_count(&cache) == CODESTREAMS * BINS / 2, "complete bins counted");
    for (codestream = 0; codestream < CODESTREAMS; codestream++) {
        for (bin_id = 0; bin_id < BINS; bin_id++) {
            const hv_bin *bin = hv_cache_find(&cache, HV_BIN_PRECINCT, codestream, bin_id);
            wrong += bin == NULL || bin->length != 2 || bin->data[0] != (uint8_t) codestream ||
                     bin->data[1] != (uint8_t) bin_id || bin->complete != (int) (bin_id % 2);
        }
    }
    Check(!wrong, "many bins found with their bytes");
    Check(hv_cache_find(&cache, HV_BIN_PRECINCT, CODESTREAMS, 0) == NULL &&
          hv_cache_find(&cache, HV_BIN_TILE_HEADER, 0, 0) == NULL, "other bins are unknown");
    hv_cache_release(&cache);
    Check(hv_cache_bin_count(&cache) == 0 && hv_cache_find(&cache, HV_BIN_PRECINCT, 0, 0) == NULL,
          "a released store is empty");
}

/* A message that is refused leaves no trace, not even an empty bin. */
static void TestRejectedLeavesNothing(void) {
    hv_cache cache;
    hv_jpp_message message;

    hv_cache_begin(&cache);
    message = Message(HV_BIN_PRECINCT, 0, 7, 3, "abc", 3, 0);
    Check(!hv_cache_apply(&cache, &message), "a bin cannot start past its first byte");
    Check(hv_cache_bin_count(&cache) == 0 && hv_cache_find(&cache, HV_BIN_PRECINCT, 0, 7) == NULL,
          "a refused message added no bin");
    hv_cache_release(&cache);
}

int main(void) {
    TestContiguousAppend();
    TestGapsRejected();
    TestDistinctBins();
    TestUnknownBinIsZero();
    TestGrowthKeepsContents();
    TestHostileLength();
    TestManyBins();
    TestRejectedLeavesNothing();
    if (failures) {
        printf("%d data-bin store check(s) failed\n", failures);
        return 1;
    }
    printf("Data-bin store checks passed\n");
    return 0;
}