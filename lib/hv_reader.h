/* hv_reader.h: steps through a JPEG 2000 file held in memory, from header
 * to header, using the decoders generated from spec/jpeg2000-io.asn1.
 *
 * The reader never copies a payload. It reports where each box, marker
 * segment, tile-part and block of tile-part data starts and ends, and it
 * checks the framing rules of T.800 Annex A and I.4 on the way. Marker
 * segment bodies (SIZ, COD, QCD, PLT, COM) are decoded and checked by the
 * generated code; the decoded SIZ and COD stay available.
 *
 * Offsets are byte offsets into the caller's buffer. */
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
 * and *at is the offset. */
int hv_boxes_next(hv_boxes *it, hv_box *box, const char **error, size_t *at);

/* Nonzero for the box types T.800 and T.801 define as superboxes. */
int hv_is_superbox(uint32_t type);

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

typedef struct {
    hv_item_kind kind;
    uint16_t code;          /* marker code; 0 for HV_TILE_DATA */
    size_t start, end;
    SotSegment sot;         /* the current tile-part's SOT (tile-part items) */
    uint32_t plt_padding;   /* HV_TILE_DATA: trailing zero PLT entries accepted */
    /* The decoded body of a SIZ, COD, QCD, PLT or COM segment, NULL
     * otherwise. Valid until the next hv_codestream_next call. */
    const SizSegment_Std *siz;
    const CodSegment_Std *cod;
    const QcdSegment_Std *qcd;
    const PltSegment_Std *plt;
    const ComSegment_Std *com;
} hv_item;

/* hv_codestream_open flags. Without HV_PROFILE_HEADERS or HV_PROFILE, the
 * reader checks T.800. Error messages that name a rule (siz.single-tile)
 * use the names of the corpus manifest, whether hv_rules.c or the reader
 * applies the rule. */
enum {
    /* Accept zero-valued PLT entries after the last packet of a tile-part.
     * T.800 forbids them (a packet has at least one byte); deployed files
     * carry them as padding (JPIP_PROFILE.md). */
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
    QcdSegment_Std qcd;     /* last decoded QCD */
    ComSegment_Std *com;    /* last decoded COM (64 KiB, allocated) */
    PltSegment_Std *plt;    /* last decoded PLT (large, allocated) */
    uint32_t tiles;         /* tiles Isot can address: min(grid, 65 535) */
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
 * flags: 0 for T.800 as written, or any of the flags above.
 * 0 on success, -1 on error (cs->error, cs->error_at). Call
 * hv_codestream_close in both cases. */
int hv_codestream_open(hv_codestream *cs, const uint8_t *buf, size_t start, size_t end,
                       unsigned flags);

/* 1: *item is the next item. 0: after HV_END. -1: invalid (cs->error). */
int hv_codestream_next(hv_codestream *cs, hv_item *item);

void hv_codestream_close(hv_codestream *cs);

/* The value of one PLT entry (7-bit groups, most significant first);
 * saturates at UINT64_MAX. */
uint64_t hv_iplt_value(const Iplt *entry);

/* Decoded main-header SIZ, and COD once the reader has reported it (NULL
 * before). */
const Siz *hv_codestream_siz(const hv_codestream *cs);
const Cod *hv_codestream_cod(const hv_codestream *cs);

#ifdef __cplusplus
}
#endif

#endif
