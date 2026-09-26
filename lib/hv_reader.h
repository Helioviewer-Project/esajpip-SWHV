/* hv_reader.h: steps through a JPEG 2000 file held in memory, from header
 * to header, using the decoders generated from spec/jpeg2000-io.asn1.
 *
 * The reader never copies a payload. It reports where each box, marker
 * segment, tile-part and block of tile-part data starts and ends, and it
 * checks the framing rules of T.800 Annex A and I.4 on the way. Marker
 * segment bodies (SIZ, COD, QCD, PLT, COM) are decoded and checked by the
 * generated code; the decoded SIZ, COD and QCD stay available.
 *
 * Offsets are byte offsets into the caller's buffer.
 *
 * Errors: a function that fails returns the rule that fails, by the name
 * hv_rules.h describes, or lowercase prose naming what failed, and sets
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

/* ------------------------------------------------------------------------
 * Boxes, T.800 I.4
 * ------------------------------------------------------------------------ */

typedef struct {
    uint32_t type;          /* TBox */
    size_t start;           /* offset of LBox */
    size_t payload;         /* first payload byte */
    size_t end;             /* just past the box */
    int to_end;             /* LBox = 0: the box runs to the end of the file */
} hv_box;

/* The boxes of one container: the whole file, or a superbox's payload. */
typedef struct {
    const uint8_t *buf;
    size_t pos, end;
    int to_end;             /* the container runs to the end of the file */
    int done;
} hv_boxes;

void hv_boxes_file(hv_boxes *it, const uint8_t *buf, size_t size);
void hv_boxes_children(hv_boxes *it, const uint8_t *buf, const hv_box *parent);

/* 1: *box is the next box. 0: no more boxes. -1: invalid; *error says why
 * and *at is the box's offset. *error and *at are written only then. */
int hv_boxes_next(hv_boxes *it, hv_box *box, const char **error, size_t *at);

/* Nonzero for the box types T.800 and T.801 define as superboxes. */
int hv_is_superbox(uint32_t type);

/* The deepest superbox nesting a tool descends into (the top level is 1),
 * so that walking the boxes recursively cannot exhaust the stack. The
 * standard sets no limit; files for the server nest at most 2 deep. */
enum { HV_BOX_DEPTH_MAX = 32 };

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
 * read. NULL on
 * success, with *jpx the codestreams (hv_jpx_free); otherwise the rule that
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
 * Both check the children of the top-level superboxes as the harness does
 * at layer 1 (hv_rule_child): no jp2c, jpch, ftbl or dtbl below the top
 * level, flst only in ftbl, url only in dtbl (not checked in jp2h, jplh,
 * uinf and asoc: uinf may hold one, T.800 I.7.3); one flst per ftbl.
 * hv_check_jp2h, for a JP2 file: one or more codestreams; exactly one jp2h,
 * before the first; its children decoded by the model's types and checked
 * by hv_rules.c; and ihdr and bpcc against the first codestream's SIZ.
 * hv_check_jpx_headers, for a JPX file: the count rules of hv_rule_jpx at
 * the standard layer (one Reader Requirements box, the third box; at most
 * one dtbl; a codestream per jpch), the Reader Requirements box's contents
 * decoded with Rreq-Std; at most one jp2h, before the first codestream or
 * header box; the children of jp2h, jplh and jpch; and each codestream's
 * header (its jpch over jp2h's defaults, T.801 M.11.6) against its SIZ
 * where the codestream is embedded.
 * Both expect framing hv_boxes_next accepts. NULL, or the rule that fails
 * (the manifest's name where it has one) and *at its offset. A header rule
 * that compares boxes (ihdr.bpcc, header.pclr-cmap, ihdr.width, ...)
 * points at the codestream's header box: its jpch, else jp2h, else (in a
 * JPX file with neither) the codestream. A codestream whose SIZ the reader
 * rejects fails with the reader's error at its offset: the header cannot
 * be checked against it. */
const char *hv_check_jp2h(const uint8_t *buf, size_t size, size_t *at);
const char *hv_check_jpx_headers(const uint8_t *buf, size_t size, size_t *at);

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

/* hv_codestream_next clears every field it does not set. */
typedef struct {
    hv_item_kind kind;
    uint16_t code;          /* marker code; 0 for HV_TILE_DATA */
    size_t start, end;
    SotSegment sot;         /* HV_TILE_PART, HV_TILE_SEGMENT, HV_TILE_DATA: the
                             * current tile-part's SOT; zero otherwise */
    uint32_t plt_padding;   /* HV_TILE_DATA: trailing zero PLT entries accepted */
    /* A decoded SIZ, COD, QCD, PLT or COM segment, NULL otherwise, in the
     * form hv_writer's hv_write_* take (the accessors below give the
     * bodies, the form hv_rules and hv_geometry take). Valid until the next
     * hv_codestream_next call. */
    const SizSegment_Std *siz;
    const CodSegment_Std *cod;
    const QcdSegment_Std *qcd;
    const PltSegment_Std *plt;
    const ComSegment_Std *com;
} hv_item;

/* hv_codestream_open flags. Without HV_PROFILE_HEADERS or HV_PROFILE, the
 * reader checks T.800 as far as it reads it: it reports unknown marker
 * codes as items and skips them, by their length (0xFF30 to 0xFF3F, which
 * have no marker segment, by the marker alone), and it counts no packets,
 * so it checks PLT against the tile-part data only. Error messages that
 * name a rule (siz.single-tile) use the names of the corpus manifest,
 * whether hv_rules.c or the reader applies the rule.
 *
 * HV_PROFILE is HV_PROFILE_HEADERS | 4. The bit 4 alone does nothing: the
 * whole profile applies only when both bits, 2 and 4, are set. The flags
 * only add rules, except HV_ACCEPT_PLT_PADDING. A marker out of place is
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
     * tile-part. Not with HV_PROFILE, which has that rule in its own scope
     * (below): hv_codestream_open refuses the pair. */
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
    HV_PROFILE = 6
};

typedef struct {
    const uint8_t *buf;
    size_t start, pos, end;
    unsigned flags;
    int state;
    size_t siz_end;         /* just past the SIZ segment */
    SizSegment_Std *siz;    /* decoded SIZ (large, allocated) */
    CodSegment_Std cod;     /* decoded main-header COD */
    CodSegment_Std tile_cod;/* decoded tile-part COD */
    QcdSegment_Std qcd;     /* decoded main-header QCD */
    QcdSegment_Std tile_qcd;/* decoded tile-part QCD */
    ComSegment_Std *com;    /* last decoded COM (64 KiB, allocated) */
    PltSegment_Std *plt;    /* last decoded PLT (large, allocated) */
    uint32_t tiles;         /* tiles Isot can address: min(grid, 65,535) */
    int cods, qcds, tile_parts;
    uint16_t *parts;        /* tile-parts seen, per tile */
    uint8_t *tnsot;         /* nonzero TNsot seen, per tile */
    SotSegment sot;         /* current tile-part */
    size_t tp_start, tp_end;
    int tp_cod, tp_qcd, plts;
    uint32_t plt_zeros;     /* zero entries in the current tile-part */
    uint64_t plt_sum;
    hv_plt_count plt_count; /* PLT entries of the codestream, for the profile */
    const char *error;
    size_t error_at;
} hv_codestream;

/* Opens the codestream in buf[start, end): checks SOC and decodes SIZ.
 * flags: 0 for T.800 as written, or a combination of the flags above other
 * than HV_ACCEPT_PLT_PADDING with HV_PROFILE, which it refuses. start
 * after end is an error too. 0 on success, -1 on error (cs->error,
 * cs->error_at). Call hv_codestream_close in both cases. */
int hv_codestream_open(hv_codestream *cs, const uint8_t *buf, size_t start, size_t end,
                       unsigned flags);

/* 1: *item is the next item. 0: after HV_END. -1: invalid (cs->error). */
int hv_codestream_next(hv_codestream *cs, hv_item *item);

/* Frees what the reader allocated. cs->error and cs->error_at stay; the
 * accessors below return NULL. */
void hv_codestream_close(hv_codestream *cs);

/* Reads the codestream in buf[start, end) through with these flags. NULL,
 * or the reader's error and *at its offset. */
const char *hv_codestream_check(const uint8_t *buf, size_t start, size_t end, unsigned flags,
                                size_t *at);

/* The value of one PLT entry (7-bit groups, most significant first);
 * saturates at UINT64_MAX. */
uint64_t hv_iplt_value(const Iplt *entry);

/* The bodies of the main header's SIZ once hv_codestream_open has
 * succeeded, and of its COD and QCD once the reader has reported them:
 * NULL before, for a segment the reader rejected, and after
 * hv_codestream_close. hv_rules and hv_geometry take these; the items
 * give the whole segments, for hv_writer. */
const Siz *hv_codestream_siz(const hv_codestream *cs);
const Cod *hv_codestream_cod(const hv_codestream *cs);
const Qcd_Std *hv_codestream_qcd(const hv_codestream *cs);

#ifdef __cplusplus
}
#endif

#endif
