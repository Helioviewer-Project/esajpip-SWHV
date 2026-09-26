/* hv_rules.h: the cross-field rules of the model on values (listed at the
 * end of ../spec/j2k-headers.asn1, ../spec/j2k-codestream.asn1 and
 * ../spec/jp2-boxes.asn1): marker segment bodies, tile-part indices, PLT
 * entries and packet counts, JPX boxes and JP2 header boxes. They are
 * written once, here, and used both by the reader (hv_reader.c) and by the
 * model's harness (../spec/harness/crossfield_impl.h, crossfield.c), which
 * labels the corpus in ../tests/vectors/j2k. The rules on structure (the
 * placement and count of segments, the PLT Zplt sequence and sums, the
 * file's first boxes) each checks in its own code, the reader on the bytes
 * and the harness on the decoded model, under the same names. With
 * HV_PROFILE the reader applies every JP2 rule the profile labels come
 * from; its T.800 mode leaves some out (see README.md, "What the reader
 * checks").
 *
 * `profile` selects the layer of a function that checks rules of both: 0
 * for the standard (T.800, T.801), nonzero for the served profile
 * (JPIP_PROFILE.md), which adds the server's restrictions. A function
 * whose rules all belong to the profile (hv_rule_tile_part_count,
 * hv_rule_url, hv_rule_flst) or all to the standard (the header box rules)
 * takes no `profile`: the caller calls it only at that layer.
 *
 * Each check returns NULL when the rules hold, otherwise the name of the
 * rule that fails, never anything else: the checks do not allocate. The
 * corpus manifest uses the same names, for the rules a vector exercises;
 * some (file.size-limit, box.extent, ...) no vector does. Two functions
 * here are not checks but the counts the checks and the reader work from:
 * hv_rule_tiles and hv_rule_packets. */
#ifndef HV_RULES_H
#define HV_RULES_H

#include <stdint.h>

#include "hv_codes.h"
#include "j2k-headers.h"
#include "jp2-boxes.h"

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

/* Not a check: the tiles of the grid SIZ describes that Isot (0 to 65,534)
 * can address, at most 65,535. 0 if SIZ has no tile grid (siz.tile-origin
 * and the other SIZ rules fail then). */
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

/* The profile's limit on the tile-parts of a codestream: the server
 * indexes at most 64 (PacketIndex::MAX_SEGMENTS; TPsot 0 to 63 in
 * ../spec/j2k-codestream.asn1). `parts` counts them so far. Profile only:
 * the standard sets no such limit. */
enum { HV_PROFILE_TILE_PARTS = 64 };
const char *hv_rule_tile_part_count(uint64_t parts);

/* The value of one PLT entry (A.7.3): 7-bit groups, most significant
 * first. "plt.value-overflow" when it does not fit 64 bits. */
const char *hv_rule_iplt(const Iplt *entry, uint64_t *value);

/* The PLT entries of a codestream, counted in codestream order. */
typedef struct {
    uint64_t packets;       /* nonzero entries */
    int padding;            /* a zero entry has been seen */
} hv_plt_count;

/* Counts one entry. The standard forbids zero lengths (a packet has at
 * least one byte: plt.zero-length); the profile accepts them as padding
 * after the last packet of the codestream (plt.padding-position for a
 * nonzero entry after a zero one). The reader's HV_ACCEPT_PLT_PADDING
 * applies plt.padding-position, under the same name, to each tile-part
 * instead of the codestream (hv_reader.h). */
const char *hv_rule_plt_entry(hv_plt_count *count, uint64_t value, int profile);

/* Not a check: the number of packets (B.6, B.7) of a codestream with one
 * tile, zero origins and unit sampling, from its SIZ and main COD; 0 when
 * it exceeds INT32_MAX, which the server's signed JPIP state cannot hold. */
uint64_t hv_rule_packets(const Siz *siz, const Sgcod *sgcod, const Spcod *spcod);

/* The counted PLT entries against hv_rule_packets: one entry per packet,
 * and in the profile a packet count the server can hold. */
const char *hv_rule_plt_packets(const hv_plt_count *count, const Siz *siz,
                                const Sgcod *sgcod, const Spcod *spcod, int profile);

/* ------------------------------------------------------------------------
 * JPX boxes, T.801 Annex M (listed at the end of ../spec/jp2-boxes.asn1)
 * ------------------------------------------------------------------------ */

/* A box inside a top-level superbox (jpch, ftbl and dtbl at both layers;
 * at the standard layer also jp2h, jplh, uinf and asoc, whose url
 * children the callers leave out, as uinf may hold one: T.800 I.7.3):
 * jp2c, jpch, ftbl and dtbl only at the top level (M.11.2, M.11.6), flst
 * only in ftbl, url only in dtbl, and a dtbl holds url boxes only. */
const char *hv_rule_child(uint32_t parent, uint32_t child);

/* The only URL scheme the profile links with, and its length. */
#define HV_FILE_SCHEME "file://"
enum { HV_FILE_SCHEME_LENGTH = sizeof HV_FILE_SCHEME - 1 };

/* The profile's Data Entry URL box (ReadUrlBox, ReadJPX): VERS and FLAG 0,
 * and LOC, `n` bytes with its NUL, a file:// URL naming a .jp2 file whose
 * path hv_url_path decodes (url.percent-encoding). */
const char *hv_rule_url(uint64_t vers, uint64_t flag, const uint8_t *loc, size_t n);

/* The path of a file:// URL: the `n` characters of LOC after the scheme,
 * percent-decoded as the server decodes them (g_uri_unescape_string): each
 * '%' starts an escape of two hex digits, and no escape gives NUL. When
 * out is not NULL, the path is written there, NUL-terminated. 0; -1 when
 * an escape is invalid; -2 when out (out_size bytes) is too small. */
int hv_url_path(const uint8_t *path, size_t n, char *out, size_t out_size);

/* The profile's Fragment List box: one fragment (ReadFlstBox). */
const char *hv_rule_flst(uint64_t nf, int fragments);

/* A fragment's data reference: 0 (this file) or at most NDR; the profile
 * links to other files only. */
const char *hv_rule_fragment_dr(uint64_t dr, uint64_t ndr, int profile);

/* The top-level boxes of a JPX file, counted. */
typedef struct {
    int jp2c, jpch, ftbl, dtbl, rreq;
    int rreq_third;         /* the third box is rreq */
} hv_jpx_boxes;

/* The file rules on those counts: at most one dtbl, a codestream per jpch
 * where there is any, and rreq, once and third (standard only); in the
 * profile, at least one jpch, embedded or linked codestreams but not both,
 * and a dtbl where they are linked. */
const char *hv_rule_jpx(const hv_jpx_boxes *boxes, int profile);

/* ------------------------------------------------------------------------
 * JP2 header boxes, T.800 I.5.3 and T.801 M.11.5 to M.11.7 (listed at the
 * end of ../spec/jp2-boxes.asn1). Standard layer only: the server reads
 * none of them.
 * ------------------------------------------------------------------------ */

/* Where the JP2 Header box is, among the top-level boxes: how many there
 * are, whether one follows the first codestream (a jp2c box in a JP2 file;
 * a jp2c, ftbl, jpch or jplh box in a JPX file, T.801 M.11.5), and, in a
 * JP2 file, how many jp2c boxes there are. JP2 (T.800 I.2, I.5.3, I.5.4):
 * one or more codestreams (jp2.one-codestream, the name the profile's
 * "exactly one" rule has too), exactly one jp2h (jp2.one-jp2h), before the
 * first codestream (jp2h.position). JPX (T.801 M.11.5): at most one jp2h
 * (jpx.one-jp2h), before the first codestream or header box. */
const char *hv_rule_jp2h_place(int jp2h, int late, int jp2c, int jpx);

/* The contents of one header box: a JP2 Header box (jp2h), or in a JPX
 * file a Codestream Header box (jpch) or Compositing Layer Header box
 * (jplh). Start with hv_header_init, then pass each child box in order to
 * hv_rule_header_child and its contents to the rule for its type; for a
 * Resolution box, pass its children to hv_rule_res_child and end it with
 * hv_rule_res_end. The first box of each type is recorded (the rules
 * require at most one of each but colr, and of ihdr only in jpch). About
 * 16 KiB. hv_header_init clears every field before bpcc_depth, which
 * therefore stays last: a new field goes above it. */
typedef struct {
    uint32_t parent;            /* HV_BOX_JP2H, HV_BOX_JPCH or HV_BOX_JPLH */
    int jpx;                    /* in a JPX file */
    int children;
    int ihdr, bpcc, colr, pclr, cmap, cdef, res;   /* boxes of each type */
    int colr_ended;             /* a box followed a colr box */
    int meth1, meth2;           /* colr boxes with METH 1, METH 2 */
    Ihdr image;                 /* the first ihdr */
    uint32_t bpcc_count;        /* entries of the first bpcc */
    uint32_t npc;               /* palette columns of the first pclr */
    uint32_t cmap_count;        /* entries of the first cmap */
    uint32_t cmap_cmp;          /* its largest CMP */
    int cmap_palette;           /* it has an entry with MTYP 1 */
    uint32_t cmap_pcol;         /* the largest PCOL of those entries */
    uint32_t cdef_cn;           /* the largest Cn of the first cdef */
    int resc, resd;             /* children of the current res box */
    uint8_t bpcc_depth[16384];  /* the first 16,384 entries of the first
                                 * bpcc (bpcc_count says how many are set);
                                 * last, see above */
} hv_header;

void hv_header_init(hv_header *h, uint32_t parent, int jpx);

/* The next child box: the box order and counts. jp2h starts with ihdr and
 * holds its colr boxes contiguously (T.800 I.5.3); a header box holds at
 * most one bpcc, pclr, cmap, cdef and res, and a jpch at most one ihdr (in
 * jp2h and jplh a later ihdr is accepted and ignored). */
const char *hv_rule_header_child(hv_header *h, uint32_t type);

/* The contents of each child type. Each checks what the box alone
 * determines and records what hv_rule_codestream_header needs. The decoded
 * values must satisfy their types' constraints (Ihdr_IsConstraintValid and
 * so on). */
const char *hv_rule_ihdr(hv_header *h, const Ihdr *ihdr);
/* The bytes a box holds after its fields (`extra` of Ihdr, Cdef,
 * Resolution and Rreq): none. type is the box's, as HV_BOX_IHDR; the rule
 * is "<box>.extent" (ihdr: "the length of the Image Header box shall be
 * 22 bytes", I.5.3.1). */
const char *hv_rule_extent(uint32_t type, uint64_t extra);
/* One bpcc entry, in order. */
const char *hv_rule_bpcc_entry(hv_header *h, uint64_t depth);
/* METH, EnumCS, and `rest`, the bytes after them. In a JP2 file, METH 1 or
 * 2, and the first colr, if enumerated, sRGB, greyscale or sYCC (I.5.3.3);
 * in a JPX file, no two enumerated or two ICC colr in jp2h (M.11.7.1). */
const char *hv_rule_colr(hv_header *h, const ColrHeader *colr, size_t rest);
/* NE and the column depths, and `entries`, the bytes of the table: NE x
 * NPC values, each padded to whole bytes. */
const char *hv_rule_pclr(hv_header *h, const PclrHeader *pclr, uint64_t entries);
/* One cmap entry, in order: PCOL 0 when MTYP is 0. */
const char *hv_rule_cmap_entry(hv_header *h, const CmapEntry *entry);
/* The n cdef entries: no two with the same Typ and Asoc, except where
 * either is 65,535 (unspecified; I.5.3.6). Takes any n without allocating:
 * the pairs are marked in a 24 KiB bitmap on the stack. */
const char *hv_rule_cdef(hv_header *h, const CdefEntry *entries, size_t n);
/* A child of a res box, then its end: resc, resd or both, each once. */
const char *hv_rule_res_child(hv_header *h, uint32_t type);
const char *hv_rule_res_end(hv_header *h);

/* The header of one codestream of a JP2 or (jpx) JPX file: its own header
 * box h (NULL for none) over the JP2 Header box's defaults (NULL for none),
 * and SIZ, the codestream's (NULL when the codestream is not at hand: its
 * checks are skipped).
 *   JP2 (T.800 I.5.3), h the jp2h and no defaults: at least one colr; bpcc
 *     exactly when BPC is 255; pclr exactly with cmap.
 *   JPX (T.801 M.11.6), h the jpch and defaults the jp2h: an ihdr in
 *     either; bpcc when BPC is 255; pclr needs cmap, and an MTYP 1 cmap
 *     entry needs pclr.
 *   Both: bpcc has NC entries; cmap maps components below NC and palette
 *     columns below NPC; cdef describes channels there are (JP2); and
 *     against SIZ, HEIGHT = Ysiz - YOsiz, WIDTH = Xsiz - XOsiz, NC = Csiz,
 *     BPC the components' common Ssiz or 255 when they differ, and each
 *     bpcc entry the component's Ssiz. */
const char *hv_rule_codestream_header(const hv_header *h, const hv_header *defaults,
                                      const Siz *siz, int jpx);

#ifdef __cplusplus
}
#endif

#endif
