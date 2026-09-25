/* tier2.h: JPEG 2000 packet headers (T.800 B.9 to B.10), decoded and
 * encoded for hv_transcode. A port of hvJP2K's jp2_packets.pyx.
 *
 * The precincts and code-blocks come from hv_geometry, the packets in
 * order from hv_geometry_packets. Per-code-block state is indexed by the
 * geometry's code-block number; contributions are stored at
 * block * nlayers + layer. */
#ifndef HV_TIER2_H
#define HV_TIER2_H

#include <stddef.h>
#include <stdint.h>

#include "hv_geometry.h"

typedef struct {
    size_t nblocks;
    int nlayers;
    int32_t *zbp, *incl;                    /* nblocks; -1 until included */
    int32_t *npasses;                       /* nblocks * nlayers; 0 = none */
    int64_t *offset, *length;               /* nblocks * nlayers */
} hv_blocks;

/* Decodes every packet header of the tile data `data` (the tile-parts'
 * data, concatenated; tile_part_ends are the cumulative ends). zero_psot:
 * the last tile-part had Psot = 0. sop/eph: Scod bits 1 and 2. Returns 0,
 * or -1 with a message in error[size]. */
int hv_read_packets(const hv_geometry *g, const hv_packet *packets, size_t npackets,
                    const uint8_t *data, size_t size, const int64_t *tile_part_ends,
                    size_t nparts, int zero_psot, int sop, int eph, hv_blocks *b,
                    char *error, size_t error_size);

/* Encodes `packets` from the contributions in `b`, copying their bytes
 * from `data`. Always writes a full header, never the one-bit empty packet
 * (as Kakadu). On success *out (malloc'd) holds *out_size bytes and
 * packet_lengths[k] the length of packet k. */
int hv_write_packets(const hv_geometry *g, const hv_packet *packets, size_t npackets,
                     const uint8_t *data, const hv_blocks *b, uint8_t **out,
                     size_t *out_size, uint64_t *packet_lengths, char *error,
                     size_t error_size);

#endif
