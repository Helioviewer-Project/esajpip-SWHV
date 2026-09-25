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
} hv_item;

/* hv_codestream_open flags. */
enum {
    /* Accept zero-valued PLT entries after the last packet of a tile-part.
     * T.800 forbids them (a packet has at least one byte); deployed files
     * carry them as padding, and the server accepts them (JPIP_PROFILE.md). */
    HV_ACCEPT_PLT_PADDING = 1
};

typedef struct {
    const uint8_t *buf;
    size_t start, pos, end;
    unsigned flags;
    int state;
    size_t siz_end;         /* just past the SIZ segment */
    SizSegment_Std *siz;    /* decoded SIZ (large, allocated) */
    CodSegment_Std cod;     /* decoded main-header COD */
    PltSegment_Std *plt;    /* scratch for PLT bodies (large, allocated) */
    uint32_t tiles;         /* tiles Isot can address: min(grid, 65 535) */
    int cods, qcds, tile_parts;
    uint16_t *parts;        /* tile-parts seen, per tile */
    uint8_t *tnsot;         /* nonzero TNsot seen, per tile */
    SotSegment sot;         /* current tile-part */
    size_t tp_start, tp_end;
    int tp_cod, tp_qcd, plts;
    uint32_t plt_zeros;     /* zero entries since the last nonzero one */
    uint64_t plt_sum;
    const char *error;
    size_t error_at;
} hv_codestream;

/* Opens the codestream in buf[start, end): checks SOC and decodes SIZ.
 * flags: 0 for T.800 as written, or HV_ACCEPT_PLT_PADDING.
 * 0 on success, -1 on error (cs->error, cs->error_at). Call
 * hv_codestream_close in both cases. */
int hv_codestream_open(hv_codestream *cs, const uint8_t *buf, size_t start, size_t end,
                       unsigned flags);

/* 1: *item is the next item. 0: after HV_END. -1: invalid (cs->error). */
int hv_codestream_next(hv_codestream *cs, hv_item *item);

void hv_codestream_close(hv_codestream *cs);

/* Decoded main-header SIZ and COD; COD is valid once the first tile-part
 * has been reported. */
const Siz *hv_codestream_siz(const hv_codestream *cs);
const Cod *hv_codestream_cod(const hv_codestream *cs);

#ifdef __cplusplus
}
#endif

#endif
