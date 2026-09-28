/* hv_reader.h: steps through a JPEG 2000 file held in memory, from header
 * to header, using the decoders generated from ../spec/ (jpeg2000-io.asn1
 * and the whole-file model's types it shares).
 *
 * The reader never copies a payload. It reports where each box, marker
 * segment, tile-part and block of tile-part data starts and ends, and it
 * checks the framing rules of T.800 Annex A and I.4 on the way. Marker
 * segment bodies (SIZ, COD, QCD, PLT, COM) are decoded and checked by the
 * generated code, COD and QCD whole, SIZ, PLT and COM one element at a
 * time up to the end Lxxx gives; the decoded SIZ, COD and QCD stay
 * available. A list is never held in a struct at the standard's bound:
 * SIZ's components are allocated as many as the segment holds, and PLT
 * entries and COM text stay in the caller's buffer.
 *
 * Offsets are byte offsets into the caller's buffer.
 *
 * Errors: a function that fails returns the rule that fails, by the name
 * the corpus manifest uses, or lowercase prose naming what failed, and sets
 * *at (or cs->error_at) to where it failed: the box, marker segment or
 * entry at fault; for a rule on the whole file (how many boxes of a type
 * there are, or where the first of them is: file.two-boxes,
 * jp2.one-codestream, the rules of hv_rule_jpx and hv_rule_jp2h_place),
 * the end of the file. A function that succeeds leaves *at as it was. */
#ifndef HV_READER_H
#define HV_READER_H

#include <stddef.h>
#include <stdint.h>

#include "hv_rules.h"
#include "jpeg2000-io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The file rules of the served profile (JPIP_PROFILE.md; the profile layer
 * of ../spec/jp2-boxes.asn1) for a JP2 file: at most INT_MAX bytes; the
 * signature box, then a file type box with the jp2 brand and a jp2
 * compatibility entry; top-level boxes framed as hv_boxes_next requires;
 * exactly one codestream box. Other boxes' contents are not checked, and a
 * raw codestream fails. NULL on success, with *jp2c the codestream box;
 * otherwise the rule that fails (the manifest's name where it has one) and
 * *at its offset. Check the codestream with hv_codestream_open and
 * HV_PROFILE. */
const char *hv_check_jp2(const uint8_t *buf, size_t size, hv_box *jp2c, size_t *at);

/* A linked codestream: the fragment of its flst box, and the LOC of the url
 * box its DR names. */
typedef struct {
    uint64_t offset, length;    /* the codestream in the linked file */
    uint64_t dr;                /* its data reference, 1 to NDR */
    const uint8_t *loc;         /* the URL, in the caller's buffer */
    size_t loc_size;            /* without its NUL */
} hv_link;

/* The codestreams of a JPX file, in box order: embedded (jp2c boxes) or
 * linked (links), never both. */
typedef struct {
    size_t count;
    hv_box *jp2c;               /* count boxes, or NULL */
    hv_link *links;             /* count links, or NULL */
} hv_jpx;

/* The file rules of the served profile for a JPX file (JPIP_PROFILE.md; the
 * profile layer of ../spec/jp2-boxes.asn1): at most INT_MAX bytes; the
 * signature, then a file type box with the jpx brand and a jpx
 * compatibility entry; top-level boxes framed as hv_boxes_next requires,
 * and the children of jpch, ftbl and dtbl; the placement and count rules of
 * hv_rules.c; one fragment per ftbl, linking to a .jp2 file through a
 * version-0 file:// URL (hv_rule_url). Other boxes, asoc included, are not
 * read. NULL on success, with *jpx the codestreams (hv_jpx_free); otherwise
 * the rule that
 * fails and *at its offset. Check each embedded codestream with
 * hv_codestream_check and HV_PROFILE, and each linked file with
 * hv_check_link. */
const char *hv_check_jpx(const uint8_t *buf, size_t size, hv_jpx *jpx, size_t *at);
void hv_jpx_free(hv_jpx *jpx);

/* The file a link names, as the server resolves it: LOC without "file://",
 * percent-decoded (hv_url_path), and, unless the decoded path is absolute,
 * relative to the directory of jpx_path (with jpx_path NULL, left relative:
 * to the current directory). NULL, with the path in out; otherwise why not
 * (url.length, url.file-scheme, url.percent-encoding, or out too small),
 * with out empty when out_size is nonzero. */
const char *hv_link_path(const hv_link *link, const char *jpx_path, char *out,
                         size_t out_size);

/* A linked file, held in buf, against its link: hv_check_jp2, its
 * codestream with HV_PROFILE, and the fragment exactly that codestream
 * (flst.source-extent). NULL, or the rule that fails and *at its offset. */
const char *hv_check_link(const uint8_t *buf, size_t size, const hv_link *link, size_t *at);

/* The JP2 header boxes (T.800 I.5.3), and in a JPX file the Reader
 * Requirements box (T.801 M.11.1): the standard layer of
 * ../spec/jp2-boxes.asn1, whose rules are hv_rules.c's. The server reads
 * none of them, but a client decodes the image by them, so a tool that
 * writes a file checks them.
 *
 * Both first check the file's start as hv_check_jp2 and hv_check_jpx do
 * (file.signature, file.ftyp-second, file.ftyp-compatibility,
 * file.two-boxes; file.ftyp-brand in a JPX file), then the box tree
 * (hv_rule_box_tree, for the file's kind), then MinV (file.ftyp-minor).
 * hv_check_jp2h, for a JP2 file (any brand but 'jpx '): one or more
 * codestreams; exactly one jp2h, before the first; its children decoded
 * by the model's types and checked by hv_rules.c; ihdr and bpcc against
 * the first codestream's SIZ and main COD; and the ihdr's IPR against
 * the file's IPR boxes (ihdr.ipr).
 * hv_check_jpx_headers, for a JPX file (the brand 'jpx '): the count
 * rules of hv_rule_jpx at the standard layer (one Reader Requirements box,
 * the third box; at most one dtbl; a codestream per jpch), then the box
 * tree, the fragments in the file (hv_rule_fragments), MinV 1, the Reader
 * Requirements box's contents decoded one part at a time (RreqHeader, the
 * features, NVF), no deprecated feature, and nothing after them
 * (rreq.extent); at most one jp2h, in a baseline file before the first
 * jp2c, ftbl, mdat, jpch and jplh; the children of jp2h, jplh and jpch, in box
 * order; each codestream's header (its jpch over jp2h's defaults, T.801
 * M.11.6) against its SIZ where the codestream is embedded; creg in every
 * jplh or none (jpx.creg); in a baseline file (jpxb) its first layer's
 * rules (hv_rule_jpxb_layer); and in a file that lists 'jp2 ' the JP2
 * rules on jp2h and the first codestream.
 * Both expect framing hv_boxes_next accepts at the top level. NULL, or the
 * rule that fails (the manifest's name where it has one) and *at its
 * offset. A header rule that compares boxes (ihdr.bpcc, header.pclr-cmap,
 * ihdr.width, ...) points at the codestream's header box: its jpch, else
 * jp2h, else (in a JPX file with neither) the codestream. A codestream
 * whose main header the reader rejects (flags 0) fails with the reader's
 * error at its offset: the header cannot be checked against it. */
const char *hv_check_jp2h(const uint8_t *buf, size_t size, size_t *at);
const char *hv_check_jpx_headers(const uint8_t *buf, size_t size, size_t *at);

/* hv_check_jpx_headers for a file whose ftyp brand is 'jpx ', else
 * hv_check_jp2h: the file's kind at the standard layer (hv_ftyp_is_jpx),
 * whatever its name. */
const char *hv_check_headers(const uint8_t *buf, size_t size, size_t *at);

/* ------------------------------------------------------------------------
 * Codestream, T.800 Annex A
 * ------------------------------------------------------------------------ */

typedef enum {
    HV_SEGMENT,             /* main-header marker (segment), SIZ first */
    HV_TILE_PART,           /* SOT: start = SOT code, end = end of the tile-part */
    HV_TILE_SEGMENT,        /* tile-part header marker segment */
    HV_TILE_DATA,           /* from the byte after SOD to the end of the tile-part */
    HV_END                  /* EOC: start = its code, end = end of the codestream */
} hv_item_kind;

/* A PLT segment: Zplt, and its Iplt entries, which stay in the caller's
 * buffer at [start, end) and are read with hv_plt_next. The reader has
 * checked them all by the time it reports the segment, unless opened with
 * HV_DEFER_PLT. */
typedef struct {
    Zplt zplt;
    size_t start, end;      /* Iplt entries in buf[start, end) */
} hv_plt;

/* The next Iplt entry of buf[*pos, end), as the standard layer reads one
 * (any number of leading zero groups, Table A.36): 1 with its value in
 * *value and *pos past it; 0 at end (*pos == end); -1 for an entry that
 * does not end within end or whose value does not fit 64 bits
 * (plt.value-overflow), *pos unchanged. */
int hv_plt_next(const uint8_t *buf, size_t *pos, size_t end, uint64_t *value);

/* A COM segment: Rcom, and its text, in place in the caller's buffer. */
typedef struct {
    Rcom rcom;
    const uint8_t *text;
    size_t size;
} hv_com;

/* hv_codestream_next clears every field it does not set. */
typedef struct {
    hv_item_kind kind;
    uint16_t code;          /* marker code; 0 for HV_TILE_DATA */
    size_t start, end;
    SotSegment sot;         /* HV_TILE_PART, HV_TILE_SEGMENT, HV_TILE_DATA: the
                             * current tile-part's SOT; zero otherwise */
    uint32_t plt_padding;   /* HV_TILE_DATA: trailing zero PLT entries accepted;
                              * unknown (zero) with HV_DEFER_PLT */
    /* A decoded SIZ, COD, QCD, PLT or COM segment, NULL otherwise, in the
     * form hv_writer's hv_write_* take (for PLT, the entries through
     * hv_plt_next; the accessors below give the COD and QCD bodies, the
     * form hv_rules and hv_geometry take). Valid until the next
     * hv_codestream_next call; siz as long as hv_codestream_siz. */
    const hv_siz *siz;
    const CodSegment *cod;
    const QcdSegment *qcd;
    const hv_plt *plt;
    const hv_com *com;
} hv_item;

/* hv_codestream_open flags. Without HV_PROFILE_HEADERS or HV_PROFILE, the
 * reader checks T.800 as far as it reads it: it reports unknown marker
 * codes as items and skips them, by their length (0xFF30 to 0xFF3F, which
 * have no marker segment, by the marker alone), and it counts packets only
 * where the layout gives them (every tile-part with PLT, one tile, zero
 * origins, unit sampling, no COC, POC, PPM or PPT, no tile-part COD, not
 * HV_ACCEPT_PLT_PADDING), as the model's standard layer. Error messages
 * that name a rule (siz.single-tile) use the names of the corpus manifest,
 * whether hv_rules.c or the reader applies the rule.
 *
 * HV_PROFILE is HV_PROFILE_HEADERS | 4. The bit 4 alone does nothing: the
 * whole profile applies only when both bits, 2 and 4, are set. The flags
 * only add rules, except HV_ACCEPT_PLT_PADDING and HV_DEFER_PLT.
 * A marker out of place is
 * main.marker-code or tile.marker-code, and a tile-part header without
 * SOD "tile-part header without SOD", whatever the flags; but the
 * profile's marker rules come first, so under HV_PROFILE a second COD in
 * a tile-part header is tile.marker-code rather than tile.cod-once. */
enum {
    /* Accept zero-valued PLT entries after the last packet of a tile-part.
     * T.800 forbids them (a packet has at least one byte); deployed files
     * carry them as padding (JPIP_PROFILE.md). A nonzero entry after a
     * zero one in the same tile-part fails as plt.padding-position: the
     * profile's rule, whose scope is the codestream, applied to each
     * tile-part; so is its Zplt order (plt.zplt-sequence). Not with
     * HV_PROFILE, which has that rule in its own scope (below):
     * hv_codestream_open refuses the pair. */
    HV_ACCEPT_PLT_PADDING = 1,
    /* The served profile's rules on the main header (JPIP_PROFILE.md; the
     * model's layer 2): SIZ as Siz-Profile, with zero origins, unit
     * sampling and one tile; no COC, POC, PPM, marker T.800 places
     * elsewhere or segment-less 0xFF30 to 0xFF3F (MainMarkerCode-Profile).
     * What a transcoder needs of its input: it rewrites the tile-parts. */
    HV_PROFILE_HEADERS = 2,
    /* The whole served profile, main header included: no SOP; tile-part
     * headers with PLT only (TileMarkerCode-Profile); PLT in every
     * tile-part; at most 64 tile-parts; one nonzero PLT entry per packet,
     * with zero entries only after the last packet of the codestream; a
     * packet count that fits INT32_MAX. */
    HV_PROFILE = 6,
    /* Structural traversal for later packet indexing. Only with HV_PROFILE.
     * Reports PLT ranges without decoding entries. PLT numbering, lengths,
     * padding, coverage and packet count remain unchecked until consumed
     * through hv_plt_reader. HV_END here means structurally complete, not
     * fully validated. hv_codestream_check refuses this flag. */
    HV_DEFER_PLT = 8
};

/* Incremental PLT validation. No allocation or ownership of the input.
 * Initialize once per codestream. Begin each segment in traversal order,
 * then read it to completion before beginning another. A value of zero is
 * padding, not a packet. The input bytes must remain alive and unchanged
 * while read; copy hv_plt descriptors before advancing the structural reader.
 *
 * With HV_PROFILE, end_tile checks coverage and resets the tile-part state;
 * end checks the total packet count using SIZ and COD. Neither may be omitted
 * when claiming full validation. Pausing before completion is allowed, but
 * leaves the unread suffix unvalidated. Errors are terminal until init.
 *
 * Eager traversal uses this same decoder with its own standard-layer
 * tile-part checks. end_tile/end are for HV_PROFILE consumers only. */
typedef struct {
    const uint8_t *buf;
    size_t pos, end;
    unsigned flags;
    hv_zplt zplt;
    hv_plt_count count;
    uint64_t sum;
    uint32_t zeros;
    const char *rule_error; /* deferred until structural decoding completes */
    const char *error;
} hv_plt_reader;

/* Same validation flags as hv_codestream_open. Deferred consumers pass
 * HV_PROFILE; HV_DEFER_PLT affects structural traversal only. */
void hv_plt_init(hv_plt_reader *reader, unsigned flags);
/* NULL or reader->error; ranges must come from the structural reader. */
const char *hv_plt_begin(hv_plt_reader *reader, const uint8_t *buf, const hv_plt *plt);
/* 1 with a length, 0 at segment end, -1 with reader->error. On a rule
 * violation, drains the rest of that segment before failing so malformed
 * encoding still takes precedence over Zplt and entry rules. */
int hv_plt_read(hv_plt_reader *reader, uint64_t *value);
const char *hv_plt_end_tile(hv_plt_reader *reader, size_t data_size);
const char *hv_plt_end(hv_plt_reader *reader, const hv_siz *siz, const Cod *cod);

typedef struct {
    const uint8_t *buf;
    size_t pos, end;
    unsigned flags;
    int state;
    size_t siz_end;         /* just past the SIZ segment */
    SizFixed siz_fixed;     /* decoded SIZ: its fixed part, */
    Component *components;  /* its components (allocated), */
    hv_siz siz;             /* and the two as hv_rules takes them */
    CodSegment cod;     /* decoded main-header COD */
    CodSegment tile_cod;/* decoded tile-part COD */
    QcdSegment qcd;     /* decoded main-header QCD */
    QcdSegment tile_qcd;/* decoded tile-part QCD */
    hv_com com;             /* last decoded COM */
    hv_plt plt;             /* last decoded PLT */
    uint32_t tiles;         /* tiles Isot can address: min(grid, 65,535) */
    int cods, qcds, tile_parts;
    /* The tile-parts seen, per tile: tile i's counts are
     * tile_pages[i / 256][i % 256]. The ceil(tiles / 256) page pointers are
     * allocated with the first tile-part, each page (of 256 tiles, the
     * last of the rest) when one of its tiles first appears, so that the
     * work of a codestream is bounded by its tile-parts, not by the tiles
     * its SIZ declares. */
    hv_tile_count **tile_pages;
    uint32_t unfinished;    /* tiles short of their TNsot (hv_rule_tile_part) */
    SotSegment sot;         /* current tile-part */
    size_t tp_start, tp_end;
    int tp_cod, tp_qcd, plts;
    hv_plt_reader plt_reader;
    int part_without_plt;   /* a tile-part header held no PLT */
    int layout_override;    /* COC, POC or PPM in the main header, or COD,
                             * COC, POC or PPT in a tile-part header */
    hv_segments segments;   /* the rules across segments (hv_rules.h) */
    hv_component *segment_components;   /* theirs, one per component
                                         * (allocated), NULL with HV_PROFILE */
    const char *error;
    size_t error_at;
} hv_codestream;

/* Opens the codestream in buf[start, end): checks SOC and decodes SIZ.
 * flags: 0 for T.800 as written, or a combination of the flags above other
 * than HV_ACCEPT_PLT_PADDING with HV_PROFILE, which it refuses.
 * HV_DEFER_PLT requires HV_PROFILE. start after end is an error too.
 * 0 on success, -1 on error (cs->error, cs->error_at).
 * Call hv_codestream_close in both cases. */
int hv_codestream_open(hv_codestream *cs, const uint8_t *buf, size_t start, size_t end,
                       unsigned flags);

/* 1: *item is the next item. 0: after HV_END. -1: invalid (cs->error). */
int hv_codestream_next(hv_codestream *cs, hv_item *item);

/* Frees what the reader allocated. cs->error and cs->error_at stay; the
 * accessors below return NULL. */
void hv_codestream_close(hv_codestream *cs);

/* Reads the codestream in buf[start, end) through with these flags, rejecting
 * HV_DEFER_PLT. NULL, or the reader's error and *at its offset. */
const char *hv_codestream_check(const uint8_t *buf, size_t start, size_t end, unsigned flags,
                                size_t *at);

/* The main header's SIZ once hv_codestream_open has succeeded (NULL until
 * SIZ is accepted), and the bodies of its COD and QCD once the reader has
 * reported them: NULL before, for a segment the reader rejected, and after
 * hv_codestream_close. hv_rules and hv_geometry take these; the items
 * give the whole COD and QCD segments, for hv_writer. */
const hv_siz *hv_codestream_siz(const hv_codestream *cs);
const Cod *hv_codestream_cod(const hv_codestream *cs);
const Qcd *hv_codestream_qcd(const hv_codestream *cs);

#ifdef __cplusplus
}
#endif

#endif
