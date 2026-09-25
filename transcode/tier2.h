/* tier2.h: packets (T.800 B.9, B.10) of one tile, decoded and encoded
 * without touching the code-block data: the Tier-2 part of hv_transcode.
 * A port of hvJP2K's jp2_packets.pyx.
 *
 * The precincts and code-blocks come from hv_geometry, the packets in
 * order from hv_geometry_packets. Code-blocks are indexed by the
 * geometry's code-block number. */
#ifndef HV_TIER2_H
#define HV_TIER2_H

#include <stddef.h>
#include <stdint.h>

#include "hv_geometry.h"
#include "hv_writer.h"

#define HV_NONE SIZE_MAX

/* One tile-part's data (after SOD): buf[start, end). */
typedef struct {
    size_t start, end;
} hv_span;

/* A tile's data: its tile-parts, in order, in buf. */
typedef struct {
    const uint8_t *buf;
    const hv_span *parts;
    size_t nparts;
    int zero_psot;              /* the last tile-part has Psot = 0 */
} hv_tile_data;

/* A code-block's contribution to one layer (B.10.6, B.10.7). */
typedef struct {
    size_t offset;              /* its bytes: buf[offset, offset + length) */
    uint64_t length;
    size_t next;                /* the code-block's next contribution, or HV_NONE */
    uint16_t layer;
    uint8_t passes;             /* coding passes, 1 to 164 */
} hv_contribution;

/* A code-block's state across the packets of its precinct. */
typedef struct {
    int32_t first_layer;        /* layer of its first inclusion, -1 if none (B.10.4) */
    int32_t zero_planes;        /* missing most significant bit-planes (B.10.5) */
    size_t first, last;         /* its contributions in layer order, or HV_NONE */
} hv_block;

/* Every code-block of a tile and the contributions decoded for them. */
typedef struct {
    hv_block *blocks;
    size_t nblocks;
    hv_contribution *contrib;
    size_t ncontrib, capacity;
} hv_codeblocks;

/* Allocates nblocks code-blocks, none included yet. 0, or -1. */
int hv_codeblocks_init(hv_codeblocks *cb, size_t nblocks);
void hv_codeblocks_free(hv_codeblocks *cb);

/* Decodes the packet headers of a tile and records every contribution in
 * cb. A packet may not span tile-parts. sop/eph: Scod bits 1 and 2 (SOP
 * and EPH markers may be present; they are skipped). Returns 0, or -1 with
 * a message in error. */
int hv_read_packets(const hv_geometry *g, const hv_packet *packets, size_t npackets,
                    const hv_tile_data *tile, int sop, int eph, hv_codeblocks *cb,
                    char *error, size_t error_size);

/* Encodes `packets` from the contributions in cb and sets lengths[k] to
 * the length of packet k. With out, also appends the packets to it, with
 * the contributions' bytes from buf; without, only computes the lengths
 * (for a PLT written before the packets). Always writes a full header,
 * never the one-bit empty packet, as Kakadu does. Returns 0, or -1 with a
 * message in error. */
int hv_write_packets(const hv_geometry *g, const hv_packet *packets, size_t npackets,
                     const uint8_t *buf, const hv_codeblocks *cb, hv_out *out,
                     uint64_t *lengths, char *error, size_t error_size);

#endif
