/* hv_reader.c: see hv_reader.h. */
#include "hv_reader.h"

#include "j2k-codestream.h"   /* MainMarkerCode-Profile, TileMarkerCode-Profile */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Generated decoders on a bounded window
 * ------------------------------------------------------------------------ */

/* Attaches exactly `size` bytes at `pos`, so a decoder can never read past
 * the enclosing box, segment or codestream. */
#define DECODE(T, value, buf, pos, size)                                     \
    decode_##T((value), (buf) + (pos), (size))

#define DEFINE_DECODE(T)                                                     \
    static int decode_##T(T *value, const uint8_t *at, size_t size) {       \
        BitStream s;                                                         \
        int err = 0;                                                         \
        if (size > (size_t)LONG_MAX) size = (size_t)LONG_MAX;               \
        BitStream_AttachBuffer(&s, (unsigned char *)at, (long)size);        \
        return T##_ACN_Decode(value, &s, &err) ? 0 : -1;                   \
    }

DEFINE_DECODE(BoxHeader)
DEFINE_DECODE(MarkerCode)
DEFINE_DECODE(SegmentLength)
DEFINE_DECODE(SotSegment)
DEFINE_DECODE(SizSegment_Std)
DEFINE_DECODE(CodSegment_Std)
DEFINE_DECODE(QcdSegment_Std)
DEFINE_DECODE(PltSegment_Std)
DEFINE_DECODE(ComSegment_Std)

static size_t min_size(size_t a, size_t b) { return a < b ? a : b; }

/* ------------------------------------------------------------------------
 * Boxes
 * ------------------------------------------------------------------------ */

void hv_boxes_file(hv_boxes *it, const uint8_t *buf, size_t size) {
    it->buf = buf;
    it->pos = 0;
    it->end = size;
    it->to_end = 1;
    it->done = 0;
}

void hv_boxes_children(hv_boxes *it, const uint8_t *buf, const hv_box *parent) {
    it->buf = buf;
    it->pos = parent->payload;
    it->end = parent->end;
    it->to_end = parent->to_end;
    it->done = 0;
}

int hv_boxes_next(hv_boxes *it, hv_box *box, const char **error, size_t *at) {
    BoxHeader h;
    size_t avail, header, size;

    if (it->done || it->pos == it->end)
        return 0;
    avail = it->end - it->pos;
    *at = it->pos;
    if (DECODE(BoxHeader, &h, it->buf, it->pos, min_size(avail, 16)) != 0) {
        *error = "invalid or truncated box header";
        return -1;
    }
    if (h.lbox == 1) {
        header = 16;
        if (h.xlbox > avail) {
            *error = "box overruns its container";
            return -1;
        }
        size = (size_t)h.xlbox;
    } else if (h.lbox == 0) {
        /* I.4: LBox = 0 only for the last box, and a box inside a superbox
         * may use it only if the superbox does too. */
        if (!it->to_end) {
            *error = "LBox = 0 inside a box that does not run to the end of the file";
            return -1;
        }
        header = 8;
        size = avail;
        it->done = 1;
    } else {
        header = 8;
        if (h.lbox > avail) {
            *error = "box overruns its container";
            return -1;
        }
        size = (size_t)h.lbox;
    }
    box->type = (uint32_t)h.tbox;
    box->start = it->pos;
    box->payload = it->pos + header;
    box->end = it->pos + size;
    box->to_end = h.lbox == 0;
    it->pos = box->end;
    return 1;
}

int hv_is_superbox(uint32_t type) {
    switch (type) {
    case 0x6A703268: /* jp2h, T.800 I.5.3 */
    case 0x72657320: /* res , T.800 I.5.3.7 */
    case 0x75696E66: /* uinf, T.800 I.7.3 */
    case 0x6674626C: /* ftbl, T.801 M.11.3 */
    case 0x6A706368: /* jpch, T.801 M.11.6 */
    case 0x6A706C68: /* jplh, T.801 M.11.7 */
    case 0x63677270: /* cgrp, T.801 M.11.7.1 */
    case 0x636F6D70: /* comp, T.801 M.11.10 */
    case 0x61736F63: /* asoc, T.801 M.11.11 */
    case 0x64726570: /* drep, T.801 M.11.15 */
    case 0x6A326378: /* j2cx, T.801 M.11.23 */
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------
 * JP2 file rules of the served profile
 * ------------------------------------------------------------------------ */

enum { BOX_FTYP = 0x66747970, BOX_JP2C = 0x6A703263, BRAND_JP2 = 0x6A703220 };

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* The rule names are those of the file rules in
 * ../spec/harness/crossfield_impl.h. */
const char *hv_check_jp2(const uint8_t *buf, size_t size, hv_box *jp2c, size_t *at) {
    static const uint8_t signature[12] = {0, 0, 0, 12, 0x6A, 0x50, 0x20, 0x20,
                                          0x0D, 0x0A, 0x87, 0x0A};
    hv_boxes it;
    hv_box box;
    const char *error = NULL;
    size_t i, n = 0;
    int status, codestreams = 0, compatible = 0;

    *at = 0;
    if (size > INT_MAX)
        return "file.size-limit";
    if (size < 12 || memcmp(buf, signature, 12) != 0)
        return "file.signature";
    hv_boxes_file(&it, buf, size);
    while ((status = hv_boxes_next(&it, &box, &error, at)) == 1) {
        *at = box.start;
        if (n == 1) {
            /* Brand, MinV, then the compatibility list (I.5.2). */
            size_t length = box.end - box.payload;
            if (box.type != BOX_FTYP)
                return "file.ftyp-second";
            if (length < 12 || length % 4 != 0)
                return "ftyp: contents shorter than 12 bytes or not a multiple of 4";
            if (get32(buf + box.payload) != BRAND_JP2)
                return "file.ftyp-brand";
            for (i = box.payload + 8; i < box.end; i += 4)
                compatible |= get32(buf + i) == BRAND_JP2;
            if (!compatible)
                return "file.ftyp-compatibility";
        }
        if (box.type == BOX_JP2C && codestreams++ == 0)
            *jp2c = box;
        n++;
    }
    if (status < 0)
        return error;
    if (n < 2) {
        *at = size;
        return "file.two-boxes";
    }
    if (codestreams != 1) {
        *at = size;
        return "jp2.one-codestream";
    }
    return NULL;
}

/* ------------------------------------------------------------------------
 * Codestream
 * ------------------------------------------------------------------------ */

enum {
    SOC = 0xFF4F, SIZ = 0xFF51, COD = 0xFF52, COC = 0xFF53, TLM = 0xFF55,
    PLM = 0xFF57, PLT = 0xFF58, QCD = 0xFF5C, QCC = 0xFF5D, RGN = 0xFF5E,
    POC = 0xFF5F, PPM = 0xFF60, PPT = 0xFF61, CRG = 0xFF63, COM = 0xFF64,
    SOT = 0xFF90, SOP = 0xFF91, EPH = 0xFF92, SOD = 0xFF93, EOC = 0xFFD9
};

enum { ST_SIZ, ST_MAIN, ST_TILE_HEADER, ST_AFTER_DATA, ST_DONE };

static int fail(hv_codestream *cs, const char *error, size_t at) {
    cs->error = error;
    cs->error_at = at;
    cs->state = ST_DONE;
    return -1;
}

/* The served profile, in the scope the flags ask for. */
static int profile_headers(const hv_codestream *cs) { return (cs->flags & HV_PROFILE_HEADERS) != 0; }
static int profile_full(const hv_codestream *cs) { return (cs->flags & HV_PROFILE) == HV_PROFILE; }

/* SIZ: the shared cross-field rules (hv_rules.c), and in the profile the
 * model's Siz-Profile type; then the tiles Isot can address. */
static const char *check_siz(const hv_codestream *cs, const Siz *s, uint32_t *tiles) {
    const char *error;
    int err;
    if ((error = hv_rule_siz(s, NULL, profile_headers(cs))) != NULL)
        return error;
    if (profile_headers(cs) && !Siz_Profile_IsConstraintValid(s, &err))
        return "SIZ: outside Siz-Profile";
    /* Isot (0 to 65 534) can address at most 65 535 tiles; a larger grid
     * is not an error by itself, its other tiles just cannot appear. */
    *tiles = hv_rule_tiles(s);
    return NULL;
}

/* COD: the shared cross-field rules, with the SIZ they depend on. SOP is
 * outside the profile, but only the full scope checks it: a transcoder's
 * input may carry SOP, its output may not. */
static const char *check_cod(const hv_codestream *cs, const Cod *c) {
    const char *error = hv_rule_cod(&c->scod, &c->spcod, profile_full(cs));
    return error ? error : hv_rule_siz(&cs->siz->body, &c->sgcod, profile_headers(cs));
}

uint64_t hv_iplt_value(const Iplt *p) {
    uint64_t v;
    return hv_rule_iplt(p, &v) == NULL ? v : UINT64_MAX;
}

int hv_codestream_open(hv_codestream *cs, const uint8_t *buf, size_t start, size_t end,
                       unsigned flags) {
    MarkerCode m;
    SegmentLength l;
    const char *error;

    memset(cs, 0, sizeof *cs);
    cs->buf = buf;
    cs->start = start;
    cs->end = end;
    cs->flags = flags;
    if (DECODE(MarkerCode, &m, buf, start, min_size(end - start, 2)) != 0 || m != SOC)
        return fail(cs, "codestream does not start with SOC", start);
    cs->pos = start + 2;
    if (DECODE(MarkerCode, &m, buf, cs->pos, min_size(end - cs->pos, 2)) != 0 || m != SIZ)
        return fail(cs, "SOC is not followed by SIZ", cs->pos);
    if (DECODE(SegmentLength, &l, buf, cs->pos + 2, min_size(end - cs->pos - 2, 2)) != 0 ||
        l > end - cs->pos - 2)
        return fail(cs, "truncated SIZ", cs->pos);
    cs->siz = calloc(1, sizeof *cs->siz);
    if (cs->siz == NULL)
        return fail(cs, "out of memory", cs->pos);
    if (DECODE(SizSegment_Std, cs->siz, buf, cs->pos + 2, l) != 0)
        return fail(cs, "invalid SIZ", cs->pos);
    if ((error = check_siz(cs, &cs->siz->body, &cs->tiles)) != NULL)
        return fail(cs, error, cs->pos);
    cs->siz_end = cs->pos + 2 + (size_t)l;
    cs->parts = calloc(cs->tiles, sizeof *cs->parts);
    cs->tnsot = calloc(cs->tiles, sizeof *cs->tnsot);
    if (cs->parts == NULL || cs->tnsot == NULL)
        return fail(cs, "out of memory", cs->pos);
    cs->state = ST_SIZ;
    return 0;
}

void hv_codestream_close(hv_codestream *cs) {
    free(cs->siz);
    free(cs->com);
    free(cs->plt);
    free(cs->parts);
    free(cs->tnsot);
    cs->siz = NULL;
    cs->com = NULL;
    cs->plt = NULL;
    cs->parts = NULL;
    cs->tnsot = NULL;
}

const Siz *hv_codestream_siz(const hv_codestream *cs) { return cs->siz ? &cs->siz->body : NULL; }
const Cod *hv_codestream_cod(const hv_codestream *cs) { return cs->cods ? &cs->cod.body : NULL; }

/* Reads the marker segment at cs->pos: *code, and *end just past it. */
static int read_segment(hv_codestream *cs, size_t limit, uint16_t *code, size_t *end) {
    MarkerCode m;
    SegmentLength l;
    size_t pos = cs->pos;

    if (DECODE(MarkerCode, &m, cs->buf, pos, min_size(limit - pos, 2)) != 0)
        return fail(cs, "expected a marker", pos);
    *code = (uint16_t)m;
    if (m == SOC || m == SOT || m == SOD || m == EOC || m == SOP || m == EPH ||
        (m >= 0xFF30 && m <= 0xFF3F)) {
        *end = pos + 2;               /* no marker segment */
        return 0;
    }
    if (DECODE(SegmentLength, &l, cs->buf, pos + 2, min_size(limit - pos - 2, 2)) != 0 ||
        l > limit - pos - 2)
        return fail(cs, "marker segment overruns its header", pos);
    *end = pos + 2 + (size_t)l;
    return 0;
}

static int start_tile_part(hv_codestream *cs, hv_item *item) {
    SotSegment *sot = &cs->sot;
    size_t pos = cs->pos;
    hv_tile_parts seen = {cs->tiles, cs->parts, cs->tnsot};
    const char *error;

    if (cs->state == ST_MAIN && (cs->cods != 1 || cs->qcds != 1))
        return fail(cs, "main header needs exactly one COD and one QCD", pos);
    if (DECODE(SotSegment, sot, cs->buf, pos + 2, min_size(cs->end - pos - 2, 10)) != 0)
        return fail(cs, "invalid SOT", pos);
    if (profile_full(cs) && cs->tile_parts == 64)
        return fail(cs, "codestream.tile-part-limit", pos);
    if ((error = hv_rule_tile_part(&seen, sot->isot, sot->tpsot, sot->tnsot)) != NULL)
        return fail(cs, error, pos);
    if (sot->psot == 0) {
        /* A.4.2: the last tile-part; its data runs up to the EOC that ends
         * the codestream. */
        if (cs->end - pos < 16 || cs->buf[cs->end - 2] != 0xFF || cs->buf[cs->end - 1] != 0xD9)
            return fail(cs, "Psot = 0 but the codestream does not end with EOC", pos);
        cs->tp_end = cs->end - 2;
    } else {
        if (sot->psot > cs->end - 2 - pos)
            return fail(cs, "tile-part overruns the codestream", pos);
        cs->tp_end = pos + (size_t)sot->psot;
    }
    cs->tile_parts++;
    cs->tp_start = pos;
    cs->tp_cod = cs->tp_qcd = cs->plts = 0;
    cs->plt_zeros = 0;
    cs->plt_sum = 0;
    cs->pos = pos + 12;
    cs->state = ST_TILE_HEADER;
    item->kind = HV_TILE_PART;
    item->code = SOT;
    item->start = pos;
    item->end = cs->tp_end;
    item->sot = *sot;
    return 1;
}

static int decode_com(hv_codestream *cs, size_t pos, size_t len) {
    if (cs->com == NULL && (cs->com = malloc(sizeof *cs->com)) == NULL)
        return fail(cs, "out of memory", pos);
    if (DECODE(ComSegment_Std, cs->com, cs->buf, pos + 2, len) != 0)
        return fail(cs, "invalid COM", pos);
    return 0;
}

static int main_segment(hv_codestream *cs, uint16_t code, size_t end, hv_item *item) {
    size_t pos = cs->pos, len = end - pos - 2;
    const char *error;
    MainMarkerCode_Profile profile_code = code;
    int err;

    if (profile_headers(cs) && !MainMarkerCode_Profile_IsConstraintValid(&profile_code, &err))
        return fail(cs, "main header: marker outside MainMarkerCode-Profile", pos);
    switch (code) {
    case COD:
        if (++cs->cods > 1)
            return fail(cs, "second COD in the main header", pos);
        if (DECODE(CodSegment_Std, &cs->cod, cs->buf, pos + 2, len) != 0)
            return fail(cs, "invalid COD", pos);
        if ((error = check_cod(cs, &cs->cod.body)) != NULL)
            return fail(cs, error, pos);
        item->cod = &cs->cod;
        break;
    case QCD:
        if (++cs->qcds > 1)
            return fail(cs, "second QCD in the main header", pos);
        if (DECODE(QcdSegment_Std, &cs->qcd, cs->buf, pos + 2, len) != 0)
            return fail(cs, "invalid QCD", pos);
        item->qcd = &cs->qcd;
        break;
    case COM:
        if (decode_com(cs, pos, len) != 0)
            return -1;
        item->com = cs->com;
        break;
    case COC: case QCC: case RGN: case POC: case TLM: case PLM: case PPM: case CRG:
        break;                        /* bodies not modelled: skipped by length */
    case SOC: case SIZ: case SOD: case SOP: case EPH:
        return fail(cs, "marker not allowed in the main header", pos);
    case PLT: case PPT:
        return fail(cs, "tile-part header marker in the main header", pos);
    default:
        break;                        /* unknown: reported, skipped by length */
    }
    item->kind = HV_SEGMENT;
    item->code = code;
    item->start = pos;
    item->end = end;
    cs->pos = end;
    return 1;
}

static int tile_segment(hv_codestream *cs, uint16_t code, size_t end, hv_item *item) {
    size_t pos = cs->pos, len = end - pos - 2;
    const char *error;
    TileMarkerCode_Profile profile_code = code;
    int err;

    if (profile_full(cs) && !TileMarkerCode_Profile_IsConstraintValid(&profile_code, &err))
        return fail(cs, "tile-part header: marker outside TileMarkerCode-Profile", pos);
    switch (code) {
    case COD:
        if (cs->sot.tpsot != 0 || cs->tp_cod++)
            return fail(cs, "COD only once, in the first tile-part of a tile", pos);
        if (DECODE(CodSegment_Std, &cs->tile_cod, cs->buf, pos + 2, len) != 0)
            return fail(cs, "invalid COD", pos);
        if ((error = check_cod(cs, &cs->tile_cod.body)) != NULL)
            return fail(cs, error, pos);
        item->cod = &cs->tile_cod;
        break;
    case QCD:
        if (cs->sot.tpsot != 0 || cs->tp_qcd++)
            return fail(cs, "QCD only once, in the first tile-part of a tile", pos);
        if (DECODE(QcdSegment_Std, &cs->qcd, cs->buf, pos + 2, len) != 0)
            return fail(cs, "invalid QCD", pos);
        item->qcd = &cs->qcd;
        break;
    case PLT: {
        /* HV_ACCEPT_PLT_PADDING alone takes zero entries after the last
         * packet of each tile-part; the profile, after the last packet of
         * the codestream (hv_rule_plt_entry). */
        int i, per_tile_part = (cs->flags & HV_ACCEPT_PLT_PADDING) && !profile_full(cs);
        if (cs->plt == NULL && (cs->plt = malloc(sizeof *cs->plt)) == NULL)
            return fail(cs, "out of memory", pos);
        if (DECODE(PltSegment_Std, cs->plt, cs->buf, pos + 2, len) != 0)
            return fail(cs, "invalid PLT", pos);
        if (cs->plt->body.zplt != (asn1SccUint)cs->plts++)
            return fail(cs, "plt.zplt-sequence", pos);
        for (i = 0; i < cs->plt->body.entries.nCount; i++) {
            uint64_t v;
            if ((error = hv_rule_iplt(&cs->plt->body.entries.arr[i], &v)) != NULL)
                return fail(cs, error, pos);
            if (per_tile_part) {
                if (v != 0 && cs->plt_zeros != 0)
                    return fail(cs, "plt.padding-position", pos);
            } else if ((error = hv_rule_plt_entry(&cs->plt_count, v, profile_full(cs))) != NULL) {
                return fail(cs, error, pos);
            }
            if (v == 0)
                cs->plt_zeros++;
            else
                cs->plt_sum = v > UINT64_MAX - cs->plt_sum ? UINT64_MAX : cs->plt_sum + v;
        }
        item->plt = cs->plt;
        break;
    }
    case COM:
        if (decode_com(cs, pos, len) != 0)
            return -1;
        item->com = cs->com;
        break;
    case COC: case QCC: case RGN: case POC: case PPT:
        break;
    case SOT: case EOC:
        return fail(cs, "tile-part header without SOD", pos);
    case SOC: case SIZ: case TLM: case PLM: case PPM: case CRG: case SOP: case EPH:
        return fail(cs, "marker not allowed in a tile-part header", pos);
    default:
        break;
    }
    item->kind = HV_TILE_SEGMENT;
    item->code = code;
    item->start = pos;
    item->end = end;
    item->sot = cs->sot;
    cs->pos = end;
    return 1;
}

int hv_codestream_next(hv_codestream *cs, hv_item *item) {
    uint16_t code;
    size_t end;
    const char *error;

    item->siz = NULL;
    item->cod = NULL;
    item->qcd = NULL;
    item->plt = NULL;
    item->com = NULL;
    item->plt_padding = 0;
    switch (cs->state) {
    case ST_DONE:
        return cs->error ? -1 : 0;

    case ST_SIZ:
        item->kind = HV_SEGMENT;
        item->code = SIZ;
        item->start = cs->pos;
        item->end = cs->siz_end;
        item->siz = cs->siz;
        cs->pos = item->end;
        cs->state = ST_MAIN;
        return 1;

    case ST_MAIN:
    case ST_AFTER_DATA:
        if (read_segment(cs, cs->end, &code, &end) != 0)
            return -1;
        if (code == SOT)
            return start_tile_part(cs, item);
        if (code == EOC) {
            if (cs->tile_parts == 0)
                return fail(cs, "codestream without a tile-part", cs->pos);
            if (end != cs->end)
                return fail(cs, "bytes after EOC", end);
            {
                hv_tile_parts seen = {cs->tiles, cs->parts, cs->tnsot};
                if ((error = hv_rule_tile_parts_end(&seen)) != NULL)
                    return fail(cs, error, cs->pos);
            }
            if (profile_full(cs) &&
                (error = hv_rule_plt_packets(&cs->plt_count, &cs->siz->body, &cs->cod.body.sgcod,
                                             &cs->cod.body.spcod, 1)) != NULL)
                return fail(cs, error, cs->pos);
            item->kind = HV_END;
            item->code = EOC;
            item->start = cs->pos;
            item->end = end;
            cs->pos = end;
            cs->state = ST_DONE;
            return 1;
        }
        if (cs->state == ST_AFTER_DATA)
            return fail(cs, "only SOT or EOC may follow tile-part data", cs->pos);
        return main_segment(cs, code, end, item);

    case ST_TILE_HEADER:
        if (read_segment(cs, cs->tp_end, &code, &end) != 0)
            return -1;
        if (code == SOD) {
            if (profile_full(cs) && cs->plts == 0)
                return fail(cs, "codestream.no-plt", cs->tp_start);
            if (cs->plts != 0 && cs->plt_sum != cs->tp_end - end)
                return fail(cs, "plt.coverage", cs->tp_start);
            item->kind = HV_TILE_DATA;
            item->code = 0;
            item->plt_padding = cs->plt_zeros;
            item->start = end;
            item->end = cs->tp_end;
            item->sot = cs->sot;
            cs->pos = cs->tp_end;
            cs->state = ST_AFTER_DATA;
            return 1;
        }
        return tile_segment(cs, code, end, item);
    }
    return fail(cs, "internal error", cs->pos);
}
