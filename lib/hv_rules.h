/* hv_rules.h: the cross-field rules of the model on marker segment bodies
 * and tile-parts (listed at the end of ../spec/j2k-headers.asn1 and
 * ../spec/j2k-codestream.asn1). They are written once, here, and used both
 * by the reader (hv_reader.c) and by the model's harness
 * (../spec/harness/crossfield_impl.h), which labels the corpus in
 * ../tests/vectors/j2k. With HV_PROFILE the reader applies every JP2 rule
 * the profile labels come from; its T.800 mode leaves some out (see
 * README.md, "What the reader checks").
 *
 * `profile` selects the layer: 0 for the standard (T.800), nonzero for the
 * served profile (JPIP_PROFILE.md), which adds the server's restrictions.
 * Each check returns NULL when the rules hold, otherwise the name of the
 * rule that fails, as the corpus manifest spells it. */
#ifndef HV_RULES_H
#define HV_RULES_H

#include <stdint.h>

#include "j2k-headers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SIZ (A.5.1, B.3): Csiz, the image area and the tile grid. Given the Sgcod
 * of a COD (NULL before there is one), also what the multiple component
 * transform requires of the first three components (A.6.1, G.2). The
 * profile adds zero origins, unit sampling and a single tile. */
const char *hv_rule_siz(const Siz *siz, const Sgcod *sgcod, int profile);

/* COD (A.6.1): the precinct sizes and the code-block area. The profile
 * adds: no SOP markers. */
const char *hv_rule_cod(const Scod *scod, const Spcod *spcod, int profile);

/* The tiles of the grid SIZ describes that Isot (0 to 65 534) can
 * address: at most 65 535. 0 if SIZ has no tile grid (siz.tile-origin and
 * the other SIZ rules fail then). */
uint32_t hv_rule_tiles(const Siz *siz);

/* The tile-parts of a codestream, counted per tile. The caller provides
 * `tiles` (hv_rule_tiles) and two zeroed arrays of that many entries. */
typedef struct {
    uint32_t tiles;
    uint16_t *parts;        /* tile-parts seen, per tile */
    uint8_t *tnsot;         /* the nonzero TNsot seen, per tile */
} hv_tile_parts;

/* Counts one tile-part, in codestream order (A.4.2): Isot within the grid;
 * per tile, TPsot 0, 1, 2, ...; a nonzero TNsot above TPsot and the same
 * in every tile-part that gives one. */
const char *hv_rule_tile_part(hv_tile_parts *t, uint64_t isot, uint64_t tpsot, uint64_t tnsot);

/* After the last tile-part: every tile with a nonzero TNsot has that many
 * tile-parts. */
const char *hv_rule_tile_parts_end(const hv_tile_parts *t);

/* The value of one PLT entry (A.7.3): 7-bit groups, most significant
 * first. "plt.value-overflow" when it does not fit 64 bits. */
const char *hv_rule_iplt(const Iplt *entry, uint64_t *value);

/* The PLT entries of a codestream, counted in codestream order. */
typedef struct {
    uint64_t packets;       /* nonzero entries */
    int padding;            /* a zero entry has been seen */
} hv_plt_count;

/* Counts one entry. The standard forbids zero lengths (a packet has at
 * least one byte); the profile accepts them as padding after the last
 * packet of the codestream. */
const char *hv_rule_plt_entry(hv_plt_count *count, uint64_t value, int profile);

/* The number of packets (B.6, B.7) of a codestream with one tile, zero
 * origins and unit sampling, from its SIZ and main COD; 0 when it exceeds
 * INT32_MAX, which the server's signed JPIP state cannot hold. */
uint64_t hv_rule_packets(const Siz *siz, const Sgcod *sgcod, const Spcod *spcod);

/* The counted PLT entries against hv_rule_packets: one entry per packet,
 * and in the profile a packet count the server can hold. */
const char *hv_rule_plt_packets(const hv_plt_count *count, const Siz *siz,
                                const Sgcod *sgcod, const Spcod *spcod, int profile);

#ifdef __cplusplus
}
#endif

#endif
