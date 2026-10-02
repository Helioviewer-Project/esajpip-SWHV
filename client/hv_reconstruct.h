/* hv_reconstruct.h: one codestream of a store, written as a JPEG 2000
 * codestream a decoder reads.
 *
 * The server delivers a codestream as data-bins: its main header, and for
 * each precinct one bin holding that precinct's packets, layer after layer.
 * This writes them as a codestream again: the main header, one tile-part,
 * and the precinct bins in resolution, position, component order. That is
 * the order of the bin identifiers and the RPCL progression, so the COD is
 * rewritten to RPCL and no packet is parsed. A precinct the client holds
 * nothing of gets one empty packet per layer; a decoder then reads the
 * image up to the resolution that was requested.
 *
 * For codestreams of the served profile (JPIP_PROFILE.md): one tile, zero
 * origins, unit sampling, no COC or POC. Every precinct bin held must be
 * complete. A response cut by a byte or layer limit leaves partial bins,
 * and those are refused. */
#ifndef HV_RECONSTRUCT_H
#define HV_RECONSTRUCT_H

#include <stddef.h>
#include <stdint.h>

#include "hv_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The size of codestream `codestream` of `cache` as a JPEG 2000 codestream,
 * which is written to out when capacity holds it (so out NULL and capacity
 * 0 ask for the size). 0, with a message in error, if it cannot be
 * written. */
size_t hv_reconstruct(const hv_cache *cache, uint64_t codestream, uint8_t *out,
                      size_t capacity, char *error, size_t error_size);

/* A codestream of a store, from its main header: the image's size and
 * components, its resolution levels, and how many of them, from the
 * lowest, are complete: every precinct bin of theirs is. Those are the
 * levels hv_reconstruct's codestream has the data of. All 0 until the main
 * header is complete. */
typedef struct {
    uint32_t width, height;
    int components;
    int resolutions;
    int complete;
} hv_status;

/* 0, or -1 with a message in error for a main header hv_reconstruct
 * refuses. */
int hv_reconstruct_status(const hv_cache *cache, uint64_t codestream, hv_status *status,
                          char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
