/* hv_reader.c: see hv_reader.h. */
#include "hv_reader.h"

#include "j2k-codestream.h"   /* MainMarkerCode-Profile, TileMarkerCode-Profile */
#include "jp2-boxes.h"        /* Fragment, the JP2 header boxes */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Generated decoders on a bounded window
 * ------------------------------------------------------------------------ */

/* Attaches exactly `size` bytes at `pos`, so a decoder can never read past
 * the enclosing box, segment or codestream. DECODE_USED also returns the
 * bytes the decoder read: the reader takes every field's position from the
 * model's layout this way, never from its own arithmetic. */
#define DECODE(T, value, buf, pos, size)                                     \
    decode_##T((value), (buf) + (pos), (size), NULL)
#define DECODE_USED(T, value, buf, pos, size, used)                          \
    decode_##T((value), (buf) + (pos), (size), (used))

#define DEFINE_DECODE(T)                                                     \
    static int decode_##T(T *value, const uint8_t *at, size_t size, size_t *used) { \
        BitStream s;                                                         \
        int err = 0;                                                         \
        if (size > (size_t)LONG_MAX) size = (size_t)LONG_MAX;               \
        BitStream_AttachBuffer(&s, (unsigned char *)at, (long)size);        \
        if (!T##_ACN_Decode(value, &s, &err))                               \
            return -1;                                                       \
        if (used != NULL)                                                    \
            *used = (size_t)BitStream_GetLength(&s);                         \
        return 0;                                                            \
    }

/* A marker code (A.1.1). */
#define MARKER HV_FIXED(MarkerCode)

DEFINE_DECODE(BoxHeader)
DEFINE_DECODE(MarkerCode)
DEFINE_DECODE(SegmentLength)
DEFINE_DECODE(SotSegment)
DEFINE_DECODE(SizFixed)
DEFINE_DECODE(Component)
DEFINE_DECODE(CodSegment)
DEFINE_DECODE(QcdSegment)
DEFINE_DECODE(Zplt)
DEFINE_DECODE(Iplt)
DEFINE_DECODE(Rcom)
DEFINE_DECODE(DataReferenceCount)
DEFINE_DECODE(UrlHeader)
DEFINE_DECODE(FragmentCount)
DEFINE_DECODE(Fragment)
DEFINE_DECODE(FtypHeader)
DEFINE_DECODE(Brand)
DEFINE_DECODE(Ihdr)
DEFINE_DECODE(BitDepth)
DEFINE_DECODE(ColrHeader)
DEFINE_DECODE(PclrCounts)
DEFINE_DECODE(CmapEntry)
DEFINE_DECODE(CdefCount)
DEFINE_DECODE(CdefEntry)
DEFINE_DECODE(Resolution)
DEFINE_DECODE(RreqHeader)
DEFINE_DECODE(RreqStandardFeature)
DEFINE_DECODE(FeatureCount)
DEFINE_DECODE(RreqVendorFeature)

/* hv_codes.h's box header sizes are the model's. */
_Static_assert(HV_LARGEST(BoxHeader) == HV_BOX_HEADER_XL, "BoxHeader is LBox, TBox, XLBox");

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
    size_t avail, header = 0, size = 0;
    const char *fault = NULL;

    if (it->done || it->pos == it->end)
        return 0;
    avail = it->end - it->pos;
    if (DECODE_USED(BoxHeader, &h, it->buf, it->pos, min_size(avail, HV_BOX_HEADER_XL),
                    &header) != 0) {
        fault = "invalid or truncated box header";
    } else if (h.lbox == 1) {
        if (h.xlbox > avail)
            fault = "box overruns its container";
        size = (size_t)h.xlbox;
    } else if (h.lbox == 0) {
        /* I.4: LBox = 0 only for the last box, and a box inside a superbox
         * may use it only if the superbox does too. */
        if (!it->to_end)
            fault = "LBox = 0 inside a box that does not run to the end of the file";
        size = avail;
    } else {
        if (h.lbox > avail)
            fault = "box overruns its container";
        size = (size_t)h.lbox;
    }
    if (fault != NULL) {
        *error = fault;
        *at = it->pos;
        return -1;
    }
    it->done = h.lbox == 0;
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
    case HV_BOX_JP2H: /* T.800 I.5.3 */
    case HV_BOX_RES:  /* T.800 I.5.3.7 */
    case HV_BOX_UINF: /* T.800 I.7.3 */
    case HV_BOX_FTBL: /* T.801 M.11.3 */
    case HV_BOX_JPCH: /* T.801 M.11.6 */
    case HV_BOX_JPLH: /* T.801 M.11.7 */
    case HV_BOX_CGRP: /* T.801 M.11.7.1 */
    case HV_BOX_COMP: /* T.801 M.11.10 */
    case HV_BOX_ASOC: /* T.801 M.11.11 */
    case HV_BOX_DREP: /* T.801 M.11.15 */
    case HV_BOX_J2CX: /* T.801 M.11.23 */
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------
 * File rules of the served profile: JP2 and JPX
 * ------------------------------------------------------------------------ */

/* The size and the signature box, before any box is read: the whole box
 * is fixed (T.800 I.5.1), LBox 12 and TBox HV_BOX_JP, then these
 * contents. */
static const char *check_start(const uint8_t *buf, size_t size) {
    static const uint8_t contents[] = HV_SIGNATURE_BYTES;
    BoxHeader h;
    size_t used;
    if (size > INT_MAX)
        return "file.size-limit";
    if (DECODE_USED(BoxHeader, &h, buf, 0, min_size(size, HV_BOX_HEADER_XL), &used) != 0 ||
        h.tbox != HV_BOX_JP || h.lbox != HV_BOX_HEADER + sizeof contents ||
        size - used < sizeof contents || memcmp(buf + used, contents, sizeof contents) != 0)
        return "file.signature";
    return NULL;
}

/* The second box: File Type, with the brand and a compatibility entry
 * (T.800 I.5.2, T.801 M.11.1.2): FtypHeader, then at least one Brand,
 * filling the box. */
static const char *check_ftyp(const uint8_t *buf, const hv_box *box, uint32_t brand) {
    FtypHeader h;
    Brand entry;
    size_t length = box->end - box->payload, used, i;
    int compatible = 0;
    if (box->type != HV_BOX_FTYP)
        return "file.ftyp-second";
    if (DECODE_USED(FtypHeader, &h, buf, box->payload, length, &used) != 0 ||
        length - used < HV_FIXED(Brand) || (length - used) % HV_FIXED(Brand) != 0)
        return "ftyp: not BR, MinV and one or more compatibility entries";
    if (h.brand != brand)
        return "file.ftyp-brand";
    for (i = box->payload + used; i < box->end; i += HV_FIXED(Brand)) {
        if (DECODE(Brand, &entry, buf, i, HV_FIXED(Brand)) != 0)
            return "ftyp: invalid compatibility entry";
        compatible |= entry == brand;
    }
    return compatible ? NULL : "file.ftyp-compatibility";
}

/* The start of a file, for the header checks, which do not run the
 * profile's: the signature, ftyp with `brand` second, and two boxes at
 * least (file.*). */
static const char *check_file_start(const uint8_t *buf, size_t size, uint32_t brand,
                                    size_t *at) {
    hv_boxes it;
    hv_box box;
    const char *error;
    int status, n = 0;

    *at = 0;
    if ((error = check_start(buf, size)) != NULL)
        return error;
    hv_boxes_file(&it, buf, size);
    while ((status = hv_boxes_next(&it, &box, &error, at)) == 1 && n < 2) {
        *at = box.start;
        if (n++ == 1)
            return check_ftyp(buf, &box, brand);
    }
    if (status < 0)
        return error;
    *at = size;
    return "file.two-boxes";
}

/* The file rules, as ../spec/harness/crossfield.c names them (the harness
 * implements them on the decoded model), except file.size-limit, which the
 * corpus cannot exercise. */
static const char *check_jp2(const uint8_t *buf, size_t size, hv_box *jp2c, size_t *at) {
    hv_boxes it;
    hv_box box;
    const char *error;
    size_t n = 0;
    int status, codestreams = 0;

    *at = 0;
    if ((error = check_start(buf, size)) != NULL)
        return error;
    hv_boxes_file(&it, buf, size);
    while ((status = hv_boxes_next(&it, &box, &error, at)) == 1) {
        *at = box.start;
        if (n == 1 && (error = check_ftyp(buf, &box, HV_BRAND_JP2)) != NULL)
            return error;
        if (box.type == HV_BOX_JP2C && codestreams++ == 0)
            *jp2c = box;
        n++;
    }
    if (status < 0)
        return error;
    *at = size;
    if (n < 2)
        return "file.two-boxes";
    if (codestreams != 1)
        return "jp2.one-codestream";
    return NULL;
}

const char *hv_check_jp2(const uint8_t *buf, size_t size, hv_box *jp2c, size_t *at) {
    size_t pos;
    const char *error = check_jp2(buf, size, jp2c, &pos);
    if (error != NULL)
        *at = pos;
    return error;
}

/* Grows *p (elements of `size` bytes, *cap of them) to hold n + 1. */
static int grow(void *p, size_t *cap, size_t n, size_t size) {
    void **q = p, *r;
    size_t c = *cap ? 2 * *cap : 16;
    if (n < *cap)
        return 0;
    if ((r = realloc(*q, c * size)) == NULL)
        return -1;
    *q = r;
    *cap = c;
    return 0;
}

void hv_jpx_free(hv_jpx *jpx) {
    free(jpx->jp2c);
    free(jpx->links);
    jpx->jp2c = NULL;
    jpx->links = NULL;
    jpx->count = 0;
}

/* One url box of a dtbl: LOC recorded in url. */
static const char *check_url(const uint8_t *buf, const hv_box *box, hv_link *url) {
    UrlHeader h;
    const char *error;
    size_t n = box->end - box->payload, used;
    if (n < HV_FIXED(UrlHeader))
        return "url: shorter than VERS and FLAG";
    /* UrlHeader holds VERS and FLAG 0 only (T.800 I.7.3.2), so with its
     * bytes present the decoder fails on nothing else. */
    if (DECODE_USED(UrlHeader, &h, buf, box->payload, n, &used) != 0)
        return "url.version-flags";
    if ((error = hv_rule_url(h.vers, h.flag, buf + box->payload + used, n - used)) != NULL)
        return error;
    url->loc = buf + box->payload + used;
    url->loc_size = n - used - 1;                /* without the NUL */
    return NULL;
}

/* One flst box of an ftbl: its fragment recorded in link. */
static const char *check_flst(const uint8_t *buf, const hv_box *box, hv_link *link) {
    Fragment f;
    FragmentCount nf = 0;
    const char *error;
    size_t n = box->end - box->payload, used = n, p;
    int fragments = 0;
    /* NF, then as many whole fragments as the box holds (0 if it holds a
     * part of one), for hv_rule_flst. Fragment is a fixed-size type. */
    if (DECODE_USED(FragmentCount, &nf, buf, box->payload, n, &used) == 0) {
        for (p = box->payload + used; p < box->end; p += HV_FIXED(Fragment)) {
            if (box->end - p < HV_FIXED(Fragment)) {
                fragments = 0;
                break;
            }
            fragments++;
        }
    }
    if ((error = hv_rule_flst(nf, fragments)) != NULL)
        return error;
    /* NF is 1 and the box holds one whole fragment. Of its fields, LEN and
     * DR take every value their bytes can hold (Fragment, T.801 Table
     * M.17), so the decoder can only fail on OFF below 12. */
    if (DECODE(Fragment, &f, buf, box->payload + used, HV_FIXED(Fragment)) != 0)
        return "flst: fragment offset (OFF) below 12";
    link->offset = f.off;
    link->length = f.len;
    link->dr = f.dr;
    return NULL;
}

/* The rules of hv_rules.c, and the file rules as
 * ../spec/harness/crossfield_impl.h names them. */
static const char *check_jpx(const uint8_t *buf, size_t size, hv_jpx *jpx, size_t *at) {
    hv_boxes it, children;
    hv_box box, child;
    hv_jpx_boxes count = {0, 0, 0, 0, 0, 0};
    hv_link *urls = NULL;
    size_t *flst_at = NULL;     /* the flst box of each link, for flst.dr-* */
    size_t n = 0, nurls = 0, url_cap = 0, jp2c_cap = 0, link_cap = 0, flst_cap = 0, i;
    uint64_t ndr = 0;
    const char *error;
    int status;

    memset(jpx, 0, sizeof *jpx);
    *at = 0;
    if ((error = check_start(buf, size)) != NULL)
        return error;
    hv_boxes_file(&it, buf, size);
    while ((status = hv_boxes_next(&it, &box, &error, at)) == 1) {
        *at = box.start;
        if (n == 1 && (error = check_ftyp(buf, &box, HV_BRAND_JPX)) != NULL)
            goto fail;
        n++;
        switch (box.type) {
        case HV_BOX_RREQ:
            count.rreq++;
            count.rreq_third |= n == 3;
            break;
        case HV_BOX_JP2C:
            count.jp2c++;
            if (grow(&jpx->jp2c, &jp2c_cap, jpx->count, sizeof *jpx->jp2c) != 0)
                goto oom;
            jpx->jp2c[jpx->count++] = box;
            break;
        case HV_BOX_JPCH:
        case HV_BOX_FTBL: {
            int flsts = 0;
            if (box.type == HV_BOX_JPCH)
                count.jpch++;
            else
                count.ftbl++;
            hv_boxes_children(&children, buf, &box);
            while ((status = hv_boxes_next(&children, &child, &error, at)) == 1) {
                size_t k = (size_t)count.ftbl - 1;
                *at = child.start;
                if ((error = hv_rule_child(box.type, child.type)) != NULL)
                    goto fail;
                if (child.type != HV_BOX_FLST)
                    continue;
                if (flsts++ == 0) {
                    if (grow(&jpx->links, &link_cap, k, sizeof *jpx->links) != 0 ||
                        grow(&flst_at, &flst_cap, k, sizeof *flst_at) != 0)
                        goto oom;
                    if ((error = check_flst(buf, &child, &jpx->links[k])) != NULL)
                        goto fail;
                    flst_at[k] = child.start;
                }
            }
            if (status < 0)
                goto fail;
            *at = box.start;
            if (box.type == HV_BOX_FTBL && flsts != 1) {
                error = "ftbl.one-flst";        /* T.801 M.11.3 */
                goto fail;
            }
            break;
        }
        case HV_BOX_DTBL: {
            DataReferenceCount c;
            hv_box refs = box;
            size_t used;
            count.dtbl++;
            if (DECODE_USED(DataReferenceCount, &c, buf, box.payload, box.end - box.payload,
                            &used) != 0) {
                error = "dtbl: shorter than NDR";
                goto fail;
            }
            ndr = c;
            nurls = 0;          /* a second dtbl is jpx.one-dtbl, below */
            /* The url boxes follow NDR: walk them as the children of a box
             * whose payload starts after it. */
            refs.payload += used;
            hv_boxes_children(&children, buf, &refs);
            while ((status = hv_boxes_next(&children, &child, &error, at)) == 1) {
                *at = child.start;
                if ((error = hv_rule_child(box.type, child.type)) != NULL)
                    goto fail;
                if (grow(&urls, &url_cap, nurls, sizeof *urls) != 0)
                    goto oom;
                if ((error = check_url(buf, &child, &urls[nurls++])) != NULL)
                    goto fail;
            }
            if (status < 0)
                goto fail;
            *at = box.start;
            if (nurls != ndr) {
                error = "dtbl.ndr-count";
                goto fail;
            }
            break;
        }
        default:
            break;                              /* asoc and the rest: opaque */
        }
    }
    if (status < 0)
        goto fail;
    *at = size;
    if (n < 2) {
        error = "file.two-boxes";
        goto fail;
    }
    if ((error = hv_rule_jpx(&count, 1)) != NULL)
        goto fail;
    if (count.ftbl > 0) {
        jpx->count = (size_t)count.ftbl;
        for (i = 0; i < jpx->count; i++) {
            *at = flst_at[i];
            if ((error = hv_rule_fragment_dr(jpx->links[i].dr, ndr, 1)) != NULL)
                goto fail;
            jpx->links[i].loc = urls[jpx->links[i].dr - 1].loc;
            jpx->links[i].loc_size = urls[jpx->links[i].dr - 1].loc_size;
        }
        free(jpx->jp2c);
        jpx->jp2c = NULL;
    } else {
        free(jpx->links);
        jpx->links = NULL;
    }
    free(urls);
    free(flst_at);
    return NULL;

oom:
    error = "out of memory";
fail:
    free(urls);
    free(flst_at);
    hv_jpx_free(jpx);
    return error;
}

const char *hv_check_jpx(const uint8_t *buf, size_t size, hv_jpx *jpx, size_t *at) {
    size_t pos;
    const char *error = check_jpx(buf, size, jpx, &pos);
    if (error != NULL)
        *at = pos;
    return error;
}

const char *hv_link_path(const hv_link *link, const char *jpx_path, char *out,
                         size_t out_size) {
    size_t n;
    if (out_size == 0)
        return "no room for the linked path";
    out[0] = 0;
    if (link->loc_size <= HV_FILE_SCHEME_LENGTH)
        return "url.length";
    if (memcmp(link->loc, HV_FILE_SCHEME, HV_FILE_SCHEME_LENGTH) != 0)
        return "url.file-scheme";
    switch (hv_url_path(link->loc + HV_FILE_SCHEME_LENGTH,
                        link->loc_size - HV_FILE_SCHEME_LENGTH, out, out_size)) {
    case 0:
        break;
    case -1:
        out[0] = 0;
        return "url.percent-encoding";
    default:
        out[0] = 0;
        return "linked path longer than the caller's buffer";
    }
    n = strlen(out);
    if (out[0] != '/' && jpx_path != NULL) {
        const char *slash = strrchr(jpx_path, '/');
        size_t dir = slash ? (size_t)(slash - jpx_path) + 1 : 0;
        if (dir > out_size - n - 1) {
            out[0] = 0;
            return "linked path longer than the caller's buffer";
        }
        memmove(out + dir, out, n + 1);
        memcpy(out, jpx_path, dir);
    }
    return NULL;
}

const char *hv_codestream_check(const uint8_t *buf, size_t start, size_t end, unsigned flags,
                                size_t *at) {
    hv_codestream cs;
    hv_item item;
    const char *error = NULL;
    int status = hv_codestream_open(&cs, buf, start, end, flags);
    if (status == 0)
        while ((status = hv_codestream_next(&cs, &item)) == 1)
            ;                           /* 0 after HV_END */
    if (status < 0) {
        error = cs.error;
        *at = cs.error_at;
    }
    hv_codestream_close(&cs);
    return error;
}

const char *hv_check_link(const uint8_t *buf, size_t size, const hv_link *link, size_t *at) {
    hv_box jp2c;
    const char *error;
    if ((error = hv_check_jp2(buf, size, &jp2c, at)) != NULL ||
        (error = hv_codestream_check(buf, jp2c.payload, jp2c.end, HV_PROFILE, at)) != NULL)
        return error;
    if (link->offset != jp2c.payload || link->length != jp2c.end - jp2c.payload) {
        *at = jp2c.payload;
        return "flst.source-extent";
    }
    return NULL;
}

/* ------------------------------------------------------------------------
 * JP2 header boxes (standard layer)
 * ------------------------------------------------------------------------ */

/* The header box type for hv_rule_header_child (1: any other). */
static uint32_t header_type(uint32_t type) {
    switch (type) {
    case HV_BOX_IHDR: case HV_BOX_BPCC: case HV_BOX_COLR: case HV_BOX_PCLR:
    case HV_BOX_CMAP: case HV_BOX_CDEF: case HV_BOX_RES: case HV_BOX_JP2I: case HV_BOX_CREG:
        return type;
    default:
        return 1;
    }
}

/* One child of a header box, decoded with the model's types. *at is the
 * child box (read_header sets it), or the entry at fault in a box of
 * entries (bpcc, cmap, cdef) or the child at fault in res. The types
 * that end in `extra` (Ihdr, Resolution) are decoded from at most their
 * largest encoding, so that any number of bytes after the fields gives
 * <box>.extent. */
static const char *read_header_child(const uint8_t *buf, const hv_box *box, hv_header *h,
                                     size_t *at) {
    size_t n = box->end - box->payload, p = box->payload, i;
    const char *error = NULL;

    switch (box->type) {
    case HV_BOX_IHDR: {
        Ihdr ihdr;
        if (DECODE(Ihdr, &ihdr, buf, p, min_size(n, HV_LARGEST(Ihdr))) != 0)
            return "ihdr: shorter than its fields, or a field out of range";
        return hv_rule_ihdr(h, &ihdr);
    }
    case HV_BOX_BPCC:
        if (n == 0)
            return "bpcc: empty";
        /* Each entry a BitDepth of one byte, which hv_rule_bpcc reads in
         * place. */
        for (i = 0; i < n; i += HV_FIXED(BitDepth)) {
            BitDepth depth;
            *at = p + i;
            if (DECODE(BitDepth, &depth, buf, p + i, HV_FIXED(BitDepth)) != 0)
                return "bpcc: reserved bit depth";
        }
        return hv_rule_bpcc(h, buf + p, n);
    case HV_BOX_COLR: {
        ColrHeader colr;
        size_t used;
        if (DECODE_USED(ColrHeader, &colr, buf, p, n, &used) != 0)
            return "colr: shorter than its fields";
        return hv_rule_colr(h, &colr, n - used);
    }
    case HV_BOX_PCLR: {
        /* The rules point at the box: *at is left as it is. */
        PclrCounts counts;
        size_t used = HV_FIXED(PclrCounts);
        if (n < used || DECODE(PclrCounts, &counts, buf, p, used) != 0)
            return "pclr: no NE and NPC, or one out of range";
        error = hv_rule_pclr(h, counts.ne, counts.npc);
        for (i = 0; i < counts.npc && error == NULL; i++) {
            BitDepth depth;
            if (n - used < HV_FIXED(BitDepth))
                return "pclr: shorter than its NPC bit depths";
            if (DECODE(BitDepth, &depth, buf, p + used, HV_FIXED(BitDepth)) != 0)
                return "pclr: reserved bit depth";
            used += HV_FIXED(BitDepth);
            error = hv_rule_pclr_column(h, depth);
        }
        return error != NULL ? error : hv_rule_pclr_end(h, n - used);
    }
    case HV_BOX_CMAP:
        if (n == 0 || n % HV_FIXED(CmapEntry) != 0)
            return "cmap: not a whole number of entries";
        for (i = 0; i < n && error == NULL; i += HV_FIXED(CmapEntry)) {
            CmapEntry entry;
            *at = p + i;
            if (DECODE(CmapEntry, &entry, buf, p + i, HV_FIXED(CmapEntry)) != 0)
                return "cmap: invalid entry";
            error = hv_rule_cmap_entry(h, &entry);
        }
        return error;
    case HV_BOX_CDEF: {
        CdefCount count;
        CdefEntry *entries;
        size_t used;
        if (DECODE_USED(CdefCount, &count, buf, p, n, &used) != 0)
            return "cdef: no N, or N is 0";
        if ((n - used) / HV_FIXED(CdefEntry) < count)
            return "cdef: shorter than its N entries";
        if ((entries = malloc((size_t)count * sizeof *entries)) == NULL)
            return "out of memory";
        for (i = 0; i < count && error == NULL; i++, used += HV_FIXED(CdefEntry)) {
            *at = p + used;
            if (DECODE(CdefEntry, &entries[i], buf, p + used, HV_FIXED(CdefEntry)) != 0)
                error = "cdef: invalid entry";
        }
        if (error == NULL) {
            *at = box->start;
            if ((error = hv_rule_cdef(h, entries, (size_t)count)) == NULL)
                error = hv_rule_extent(HV_BOX_CDEF, n - used);
        }
        free(entries);
        return error;
    }
    case HV_BOX_RES: {
        hv_boxes it;
        hv_box child;
        int status;
        hv_boxes_children(&it, buf, box);
        while ((status = hv_boxes_next(&it, &child, &error, at)) == 1) {
            Resolution r;
            int resolution = child.type == HV_BOX_RESC || child.type == HV_BOX_RESD;
            *at = child.start;
            if (resolution && DECODE(Resolution, &r, buf, child.payload,
                                     min_size(child.end - child.payload,
                                              HV_LARGEST(Resolution))) != 0)
                return "res: resc or resd shorter than its fields, or a field out of range";
            if ((error = hv_rule_res_child(h, child.type)) != NULL)
                return error;
            if (resolution &&
                (error = hv_rule_extent(child.type, (uint64_t)r.extra.nCount)) != NULL)
                return error;
        }
        if (status < 0)
            return error;
        *at = box->start;
        return hv_rule_res_end(h);
    }
    default:
        return NULL;
    }
}

/* A header box (jp2h, jpch or jplh), child by child. */
static const char *read_header(const uint8_t *buf, const hv_box *box, uint32_t parent, int jpx,
                               hv_header *h, size_t *at) {
    hv_boxes it;
    hv_box child;
    const char *error;
    int status;

    hv_header_init(h, parent, jpx);
    hv_boxes_children(&it, buf, box);
    while ((status = hv_boxes_next(&it, &child, &error, at)) == 1) {
        *at = child.start;
        if ((error = hv_rule_header_child(h, header_type(child.type))) != NULL ||
            (error = read_header_child(buf, &child, h, at)) != NULL)
            return error;
    }
    return status < 0 ? error : NULL;
}

/* The children of the top-level superboxes, at the standard layer, as the
 * harness checks them at layer 1 (../spec/harness/crossfield_impl.h and
 * crossfield.c): jp2c, jpch, ftbl and dtbl belong at the top level, flst
 * in an ftbl, url in a dtbl (hv_rule_child), and an ftbl holds exactly
 * one flst (ftbl.one-flst, T.801 M.11.3). In jp2h, jplh, uinf and asoc,
 * which the server does not walk, a url is not checked: T.800 I.7.3 puts
 * one in uinf. */
static const char *check_placement(const uint8_t *buf, size_t size, size_t *at) {
    hv_boxes it, children;
    hv_box box, child;
    const char *error;
    int status, flsts;

    hv_boxes_file(&it, buf, size);
    while (hv_boxes_next(&it, &box, &error, at) == 1) {
        hv_box parent = box;
        int walked = box.type == HV_BOX_JPCH || box.type == HV_BOX_FTBL ||
                     box.type == HV_BOX_DTBL;
        if (!walked && box.type != HV_BOX_JP2H && box.type != HV_BOX_JPLH &&
            box.type != HV_BOX_UINF && box.type != HV_BOX_ASOC)
            continue;
        if (box.type == HV_BOX_DTBL) {          /* the url boxes follow NDR */
            DataReferenceCount c;
            size_t used;
            if (DECODE_USED(DataReferenceCount, &c, buf, box.payload, box.end - box.payload,
                            &used) != 0) {
                *at = box.start;
                return "dtbl: shorter than NDR";
            }
            parent.payload += used;
        }
        hv_boxes_children(&children, buf, &parent);
        flsts = 0;
        while ((status = hv_boxes_next(&children, &child, &error, at)) == 1) {
            *at = child.start;
            if ((walked || child.type != HV_BOX_URL) &&
                (error = hv_rule_child(box.type, child.type)) != NULL)
                return error;
            flsts += child.type == HV_BOX_FLST;
        }
        if (status < 0)
            return error;
        if (box.type == HV_BOX_FTBL && flsts != 1) {
            *at = box.start;
            return "ftbl.one-flst";
        }
    }
    return NULL;
}

/* The codestream in buf[start, end) against its header: h over the
 * defaults, at the header box at header_at. A codestream whose SIZ does
 * not open (hv_codestream_open, flags 0) fails with the reader's error at
 * its offset; a header rule fails at header_at. */
static const char *check_codestream_header(const uint8_t *buf, size_t start, size_t end,
                                           const hv_header *h, const hv_header *defaults,
                                           int jpx, size_t header_at, size_t *at) {
    hv_codestream cs;
    const char *error;
    if (hv_codestream_open(&cs, buf, start, end, 0) != 0) {
        error = cs.error;
        *at = cs.error_at;
    } else {
        error = hv_rule_codestream_header(h, defaults, hv_codestream_siz(&cs), jpx);
        *at = header_at;
    }
    hv_codestream_close(&cs);
    return error;
}

static const char *check_jp2h(const uint8_t *buf, size_t size, size_t *at) {
    hv_boxes it;
    hv_box box, jp2h = {0}, jp2c = {0};
    hv_header h;
    const char *error;
    int status, n = 0, late = 0, codestreams = 0, ipr = 0;

    if ((error = check_file_start(buf, size, HV_BRAND_JP2, at)) != NULL)
        return error;
    *at = 0;
    hv_boxes_file(&it, buf, size);
    while ((status = hv_boxes_next(&it, &box, &error, at)) == 1) {
        if (box.type == HV_BOX_JP2H) {
            late |= codestreams > 0;
            if (n++ == 0)
                jp2h = box;
        } else if (box.type == HV_BOX_JP2C && codestreams++ == 0) {
            jp2c = box;
        }
        ipr += box.type == HV_BOX_JP2I;
    }
    if (status < 0)
        return error;
    if ((error = check_placement(buf, size, at)) != NULL)
        return error;
    *at = size;
    if ((error = hv_rule_jp2h_place(n, late, codestreams, 0)) != NULL)
        return error;
    *at = jp2h.start;
    if ((error = read_header(buf, &jp2h, HV_BOX_JP2H, 0, &h, at)) == NULL)
        error = check_codestream_header(buf, jp2c.payload, jp2c.end, &h, NULL, 0, jp2h.start, at);
    if (error == NULL && (error = hv_rule_ihdr_ipr(&h, ipr)) != NULL)
        *at = jp2h.start;
    return error;
}

const char *hv_check_jp2h(const uint8_t *buf, size_t size, size_t *at) {
    size_t pos;
    const char *error = check_jp2h(buf, size, &pos);
    if (error != NULL)
        *at = pos;
    return error;
}

/* The contents of a Reader Requirements box, one part at a time:
 * RreqHeader (ML, FUAM, DCM, NSF), NSF standard features, NVF
 * (FeatureCount) and NVF vendor features, each feature handed exactly its
 * code and ML bytes, the mask's size (RreqMask, size deduced); and nothing
 * after them (rreq.extent). */
static const char *check_rreq(const uint8_t *buf, const hv_box *box) {
    static const char invalid[] =
        "rreq: contents are not ML, FUAM, DCM, NSF standard and NVF vendor features";
    RreqHeader h;
    RreqStandardFeature sf;
    FeatureCount nvf;
    RreqVendorFeature vf;
    size_t p = box->payload, used, ml, i;

    if (DECODE_USED(RreqHeader, &h, buf, p, box->end - p, &used) != 0)
        return invalid;
    p += used;
    ml = (size_t)h.fuam.nCount;
    for (i = 0; i < h.nsf; i++, p += HV_FIXED(FeatureCode) + ml)
        if (box->end - p < HV_FIXED(FeatureCode) + ml ||
            DECODE(RreqStandardFeature, &sf, buf, p, HV_FIXED(FeatureCode) + ml) != 0)
            return invalid;
    if (DECODE_USED(FeatureCount, &nvf, buf, p, box->end - p, &used) != 0)
        return invalid;
    p += used;
    for (i = 0; i < nvf; i++, p += HV_FIXED(VendorId) + ml)
        if (box->end - p < HV_FIXED(VendorId) + ml ||
            DECODE(RreqVendorFeature, &vf, buf, p, HV_FIXED(VendorId) + ml) != 0)
            return invalid;
    return hv_rule_extent(HV_BOX_RREQ, (uint64_t)(box->end - p));
}

/* Linear passes over the top-level boxes. The first counts them for the
 * file rules (hv_rule_jpx at the standard layer, hv_rule_jp2h_place);
 * check_placement looks into the top-level superboxes. The last reads, in
 * box order, each jplh for its own rules (its header describes a
 * compositing layer, which nothing else here checks) and each codestream
 * against its header: codestream k (jp2c or ftbl) against jpch k, which a
 * second iterator finds by moving on from jpch k - 1, over the jp2h
 * defaults. */
static const char *check_jpx_headers(const uint8_t *buf, size_t size, size_t *at) {
    hv_boxes it, next_jpch;
    hv_box box, jpch, jp2h = {0}, rreq = {0}, ftyp = {0}, jplh = {0};
    hv_jpx_boxes count = {0, 0, 0, 0, 0, 0};
    hv_header h[2], *defaults;  /* h[0]: the jpch or jplh being read */
    FtypHeader header;
    const char *error = NULL;
    int status, boxes = 0, jp2hs = 0, late = 0, later = 0, jplhs = 0, cregs = 0;

    if ((error = check_file_start(buf, size, HV_BRAND_JPX, at)) != NULL)
        return error;
    *at = 0;
    hv_boxes_file(&it, buf, size);
    while ((status = hv_boxes_next(&it, &box, &error, at)) == 1) {
        if (++boxes == 2 && box.type == HV_BOX_FTYP)
            ftyp = box;
        if (boxes == 3)
            count.rreq_third = box.type == HV_BOX_RREQ;
        switch (box.type) {
        case HV_BOX_RREQ:
            if (count.rreq++ == 0)
                rreq = box;
            break;
        case HV_BOX_JP2H:
            late |= later;
            if (jp2hs++ == 0)
                jp2h = box;
            break;
        case HV_BOX_JPCH: count.jpch++; later = 1; break;
        case HV_BOX_JP2C: count.jp2c++; later = 1; break;
        case HV_BOX_FTBL: count.ftbl++; later = 1; break;
        case HV_BOX_JPLH: later = 1; break;
        case HV_BOX_DTBL: count.dtbl++; break;
        default: break;
        }
    }
    if (status < 0)
        return error;
    *at = size;
    if ((error = hv_rule_jpx(&count, 0)) != NULL ||
        (error = check_placement(buf, size, at)) != NULL)
        return error;
    /* MinV (T.801 M.8). A second box that is no File Type box, or one
     * shorter than BR and MinV, is the file rules' (hv_check_jpx). */
    *at = ftyp.start;
    if (ftyp.type == HV_BOX_FTYP &&
        DECODE(FtypHeader, &header, buf, ftyp.payload,
               min_size(ftyp.end - ftyp.payload, HV_FIXED(FtypHeader))) == 0 &&
        (error = hv_rule_ftyp_minor(header.minor)) != NULL)
        return error;
    *at = rreq.start;
    if ((error = check_rreq(buf, &rreq)) != NULL)
        return error;
    *at = size;
    if ((error = hv_rule_jp2h_place(jp2hs, late, 0, 1)) != NULL)
        return error;
    defaults = jp2hs ? &h[1] : NULL;
    if (defaults != NULL) {
        *at = jp2h.start;
        error = read_header(buf, &jp2h, HV_BOX_JP2H, 1, defaults, at);
    }
    hv_boxes_file(&it, buf, size);
    hv_boxes_file(&next_jpch, buf, size);
    while (error == NULL && hv_boxes_next(&it, &box, &error, at) == 1) {
        const hv_header *own = NULL;
        size_t header_at = defaults ? jp2h.start : box.start;
        if (box.type == HV_BOX_JPLH) {
            *at = box.start;
            error = read_header(buf, &box, HV_BOX_JPLH, 1, h, at);
            if (jplhs++ == 0)
                jplh = box;
            cregs += h->creg > 0;
            continue;
        }
        if (box.type != HV_BOX_JP2C && box.type != HV_BOX_FTBL)
            continue;
        if (count.jpch > 0) {
            /* As many jpch as codestreams (hv_rule_jpx): there is a next. */
            while (hv_boxes_next(&next_jpch, &jpch, &error, at) == 1 &&
                   jpch.type != HV_BOX_JPCH)
                ;
            *at = jpch.start;
            if ((error = read_header(buf, &jpch, HV_BOX_JPCH, 1, h, at)) != NULL)
                break;
            own = h;
            header_at = jpch.start;
        }
        if (box.type == HV_BOX_JP2C) {
            error = check_codestream_header(buf, box.payload, box.end, own, defaults, 1,
                                            header_at, at);
        } else {
            *at = header_at;
            error = hv_rule_codestream_header(own, defaults, NULL, 1);
        }
    }
    if (error == NULL && (error = hv_rule_jpx_creg(jplhs, cregs)) != NULL)
        *at = jplh.start;
    return error;
}

const char *hv_check_jpx_headers(const uint8_t *buf, size_t size, size_t *at) {
    size_t pos;
    const char *error = check_jpx_headers(buf, size, &pos);
    if (error != NULL)
        *at = pos;
    return error;
}

/* ------------------------------------------------------------------------
 * Codestream
 * ------------------------------------------------------------------------ */

enum { ST_SIZ, ST_MAIN, ST_TILE_HEADER, ST_AFTER_DATA, ST_DONE };

static int fail(hv_codestream *cs, const char *error, size_t at) {
    cs->error = error;
    cs->error_at = at;
    cs->state = ST_DONE;
    return -1;
}

/* The served profile, in the scope the flags ask for. */
static int profile_headers(const hv_codestream *cs) {
    return (cs->flags & HV_PROFILE_HEADERS) != 0;
}
static int profile_full(const hv_codestream *cs) {
    return (cs->flags & HV_PROFILE) == HV_PROFILE;
}

/* SIZ: the shared cross-field rules (hv_rules.c), and in the profile the
 * model's Siz-Profile type, in its two parts, SizFixed-Profile and
 * Component-Profile; then the tiles Isot can address. */
static const char *check_siz(const hv_codestream *cs, const hv_siz *s, uint32_t *tiles) {
    const char *error;
    size_t i;
    int err;
    if ((error = hv_rule_siz(s, NULL, profile_headers(cs))) != NULL)
        return error;
    if (profile_headers(cs)) {
        /* What Siz-Profile adds to the rules: the image extent. The
         * components' unit sampling is siz.component-sampling, above. */
        if (!SizFixed_Profile_IsConstraintValid(s->fixed, &err))
            return "SIZ: Xsiz or Ysiz above 2,147,483,647 (Siz-Profile)";
        for (i = 0; i < s->ncomponents; i++)
            if (!Component_Profile_IsConstraintValid(&s->components[i], &err))
                return "SIZ: XRsiz or YRsiz other than 1 (Siz-Profile)";
    }
    /* Isot (0 to 65,534) can address at most 65,535 tiles; a larger grid
     * is not an error by itself, its other tiles just cannot appear. */
    *tiles = hv_rule_tiles(s);
    return NULL;
}

/* The SIZ segment in buf[start, end) after its code and Lsiz: SizFixed,
 * then components one at a time up to the end of the segment, as many as
 * a Csiz can count (the model's `count == csiz` is siz.csiz-count, in
 * hv_rule_siz). -1 when they do not decode ("invalid SIZ"). */
static int decode_siz(hv_codestream *cs, size_t start, size_t end) {
    SizFixed_csiz count;
    size_t used, n, i;
    int err;
    if (DECODE_USED(SizFixed, &cs->siz_fixed, cs->buf, start, end - start, &used) != 0)
        return -1;
    start += used;
    /* Component is a fixed-size type: the segment holds n whole ones. */
    n = (end - start) / HV_FIXED(Component);
    count = n;
    if ((end - start) % HV_FIXED(Component) != 0 || !SizFixed_csiz_IsConstraintValid(&count, &err))
        return -1;
    if ((cs->components = malloc(n * sizeof *cs->components)) == NULL)
        return -2;
    for (i = 0; i < n; i++, start += HV_FIXED(Component))
        if (DECODE(Component, &cs->components[i], cs->buf, start, HV_FIXED(Component)) != 0)
            return -1;
    cs->siz.fixed = &cs->siz_fixed;
    cs->siz.components = cs->components;
    cs->siz.ncomponents = n;
    return 0;
}

/* COD: the shared cross-field rules, with the SIZ they depend on. SOP is
 * outside the profile, but only the full scope checks it: a transcoder's
 * input may carry SOP, its output may not. */
static const char *check_cod(const hv_codestream *cs, const Cod *c) {
    const char *error = hv_rule_cod(&c->scod, &c->spcod, profile_full(cs));
    return error ? error : hv_rule_siz(&cs->siz, &c->sgcod, profile_headers(cs));
}

/* The Iplt entry at *pos, within end: its value, and *pos past it. NULL;
 * "invalid PLT" when it does not decode, or plt.value-overflow
 * (hv_rule_iplt), with *pos unchanged. */
static const char *plt_entry(const uint8_t *buf, size_t *pos, size_t end, uint64_t *value) {
    Iplt entry;
    size_t used;
    const char *error;
    if (DECODE_USED(Iplt, &entry, buf, *pos, end - *pos, &used) != 0)
        return "invalid PLT";
    if ((error = hv_rule_iplt(&entry, value)) != NULL)
        return error;
    *pos += used;
    return NULL;
}

int hv_plt_next(const uint8_t *buf, size_t *pos, size_t end, uint64_t *value) {
    if (*pos >= end)
        return 0;
    return plt_entry(buf, pos, end, value) == NULL ? 1 : -1;
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
    if (start > end)
        return fail(cs, "codestream ends before it starts", start);
    if ((flags & HV_ACCEPT_PLT_PADDING) && profile_full(cs))
        return fail(cs, "HV_ACCEPT_PLT_PADDING with HV_PROFILE", start);
    if (DECODE(MarkerCode, &m, buf, start, min_size(end - start, MARKER)) != 0 || m != HV_SOC)
        return fail(cs, "codestream does not start with SOC", start);
    cs->pos = start + MARKER;
    if (DECODE(MarkerCode, &m, buf, cs->pos, min_size(end - cs->pos, MARKER)) != 0 || m != HV_SIZ)
        return fail(cs, "SOC is not followed by SIZ", cs->pos);
    if (DECODE(SegmentLength, &l, buf, cs->pos + MARKER,
               min_size(end - cs->pos - MARKER, HV_FIXED(SegmentLength))) != 0 ||
        l > end - cs->pos - MARKER)
        return fail(cs, "truncated SIZ", cs->pos);
    switch (decode_siz(cs, cs->pos + MARKER + HV_FIXED(SegmentLength), cs->pos + MARKER + l)) {
    case 0:
        break;
    case -2:
        return fail(cs, "out of memory", cs->pos);
    default:
        return fail(cs, "invalid SIZ", cs->pos);
    }
    if ((error = check_siz(cs, &cs->siz, &cs->tiles)) != NULL)
        return fail(cs, error, cs->pos);
    cs->parts = calloc(cs->tiles, sizeof *cs->parts);
    cs->tnsot = calloc(cs->tiles, sizeof *cs->tnsot);
    if (cs->parts == NULL || cs->tnsot == NULL)
        return fail(cs, "out of memory", cs->pos);
    cs->siz_end = cs->pos + MARKER + (size_t)l;     /* open succeeded */
    cs->state = ST_SIZ;
    return 0;
}

/* Everything but the error goes: the accessors return NULL again. */
void hv_codestream_close(hv_codestream *cs) {
    const char *error = cs->error;
    size_t error_at = cs->error_at;
    free(cs->components);
    free(cs->parts);
    free(cs->tnsot);
    memset(cs, 0, sizeof *cs);
    cs->state = ST_DONE;
    cs->error = error;
    cs->error_at = error_at;
}

/* siz_end is set once hv_codestream_open succeeded, cods and qcds once the
 * main-header COD or QCD passed its checks; hv_codestream_close clears all
 * three. */
const hv_siz *hv_codestream_siz(const hv_codestream *cs) {
    return cs->siz_end ? &cs->siz : NULL;
}
const Cod *hv_codestream_cod(const hv_codestream *cs) {
    return cs->cods ? &cs->cod.body : NULL;
}
const Qcd *hv_codestream_qcd(const hv_codestream *cs) {
    return cs->qcds ? &cs->qcd.body : NULL;
}

/* Reads the marker segment at cs->pos: *code, and *end just past it. The
 * codes without a segment (SOC, SOD, EOC, EPH, 0xFF30 to 0xFF3F) end after
 * the marker; so do SOT, whose segment start_tile_part reads, and SOP,
 * which is out of place in a header and rejected. */
static int read_segment(hv_codestream *cs, size_t limit, uint16_t *code, size_t *end) {
    MarkerCode m;
    SegmentLength l;
    size_t pos = cs->pos;

    if (DECODE(MarkerCode, &m, cs->buf, pos, min_size(limit - pos, MARKER)) != 0)
        return fail(cs, "expected a marker", pos);
    *code = (uint16_t)m;
    if (m == HV_SOC || m == HV_SOT || m == HV_SOD || m == HV_EOC || m == HV_SOP || m == HV_EPH ||
        (m >= HV_NO_SEGMENT_FIRST && m <= HV_NO_SEGMENT_LAST)) {
        *end = pos + MARKER;          /* no segment to read here */
        return 0;
    }
    if (DECODE(SegmentLength, &l, cs->buf, pos + MARKER,
               min_size(limit - pos - MARKER, HV_FIXED(SegmentLength))) != 0 ||
        l > limit - pos - MARKER)
        return fail(cs, "marker segment overruns its header", pos);
    *end = pos + MARKER + (size_t)l;
    return 0;
}

static int start_tile_part(hv_codestream *cs, hv_item *item) {
    SotSegment *sot = &cs->sot;
    size_t pos = cs->pos;
    hv_tile_parts seen = {cs->tiles, cs->parts, cs->tnsot};
    const char *error;

    if (cs->state == ST_MAIN && cs->cods != 1)
        return fail(cs, "codestream.one-cod-before-sot", pos);
    if (cs->state == ST_MAIN && cs->qcds != 1)
        return fail(cs, "codestream.one-qcd-before-sot", pos);
    if (DECODE(SotSegment, sot, cs->buf, pos + MARKER,
               min_size(cs->end - pos - MARKER, HV_FIXED(SotSegment))) != 0)
        return fail(cs, "invalid SOT", pos);
    if (profile_full(cs) &&
        (error = hv_rule_tile_part_count((uint64_t)cs->tile_parts + 1)) != NULL)
        return fail(cs, error, pos);
    if ((error = hv_rule_tile_part(&seen, sot->isot, sot->tpsot, sot->tnsot)) != NULL)
        return fail(cs, error, pos);
    if (sot->psot == 0) {
        /* A.4.2: the last tile-part; its data runs up to the EOC that ends
         * the codestream. */
        MarkerCode eoc;
        if (cs->end - pos < MARKER + HV_FIXED(SotSegment) + MARKER + MARKER ||   /* SOD, EOC */
            DECODE(MarkerCode, &eoc, cs->buf, cs->end - MARKER, MARKER) != 0 || eoc != HV_EOC)
            return fail(cs, "Psot = 0 but the codestream does not end with EOC", pos);
        cs->tp_end = cs->end - MARKER;
    } else {
        if (sot->psot > cs->end - MARKER - pos)       /* EOC after it */
            return fail(cs, "tile-part overruns the codestream", pos);
        cs->tp_end = pos + (size_t)sot->psot;
    }
    cs->tile_parts++;
    cs->tp_start = pos;
    cs->tp_cod = cs->tp_qcd = cs->plts = 0;
    cs->plt_zeros = 0;
    cs->plt_sum = 0;
    cs->pos = pos + MARKER + HV_FIXED(SotSegment);
    cs->state = ST_TILE_HEADER;
    item->kind = HV_TILE_PART;
    item->code = HV_SOT;
    item->start = pos;
    item->end = cs->tp_end;
    item->sot = *sot;
    return 1;
}

/* COM at pos, ending at end: Rcom, then at least one byte of text, which
 * stays in place. */
static int decode_com(hv_codestream *cs, size_t pos, size_t end) {
    size_t body = pos + MARKER + HV_FIXED(SegmentLength), used;
    if (DECODE_USED(Rcom, &cs->com.rcom, cs->buf, body, end - body, &used) != 0 ||
        end - body - used == 0)
        return fail(cs, "invalid COM", pos);
    cs->com.text = cs->buf + body + used;
    cs->com.size = end - body - used;
    return 0;
}

static int main_segment(hv_codestream *cs, uint16_t code, size_t end, hv_item *item) {
    size_t pos = cs->pos, len = end - pos - MARKER;
    const char *error;
    MainMarkerCode_Profile profile_code = code;
    int err;

    if (profile_headers(cs) && !MainMarkerCode_Profile_IsConstraintValid(&profile_code, &err))
        return fail(cs, "main.marker-code", pos);   /* MainMarkerCode-Profile */
    switch (code) {
    case HV_COD:
        if (cs->cods > 0)
            return fail(cs, "codestream.one-cod-before-sot", pos);
        if (DECODE(CodSegment, &cs->cod, cs->buf, pos + MARKER, len) != 0)
            return fail(cs, "invalid COD", pos);
        if ((error = check_cod(cs, &cs->cod.body)) != NULL)
            return fail(cs, error, pos);
        cs->cods++;
        item->cod = &cs->cod;
        break;
    case HV_QCD:
        if (cs->qcds > 0)
            return fail(cs, "codestream.one-qcd-before-sot", pos);
        if (DECODE(QcdSegment, &cs->qcd, cs->buf, pos + MARKER, len) != 0)
            return fail(cs, "invalid QCD", pos);
        cs->qcds++;
        item->qcd = &cs->qcd;
        break;
    case HV_COM:
        if (decode_com(cs, pos, end) != 0)
            return -1;
        item->com = &cs->com;
        break;
    case HV_COC: case HV_QCC: case HV_RGN: case HV_POC: case HV_TLM: case HV_PLM: case HV_PPM:
    case HV_CRG:
        break;                        /* bodies not modelled: skipped by length */
    case HV_SOC: case HV_SIZ: case HV_SOD: case HV_SOP: case HV_EPH:
    case HV_PLT: case HV_PPT:           /* T.800 places them elsewhere (Table A.1) */
        return fail(cs, "main.marker-code", pos);
    default:
        break;                        /* unknown: reported and skipped */
    }
    item->kind = HV_SEGMENT;
    item->code = code;
    item->start = pos;
    item->end = end;
    cs->pos = end;
    return 1;
}

static int tile_segment(hv_codestream *cs, uint16_t code, size_t end, hv_item *item) {
    size_t pos = cs->pos, len = end - pos - MARKER;
    const char *error;
    TileMarkerCode_Profile profile_code = code;
    int err;

    /* A header that runs into the next tile-part or the end gets the same
     * message with every flag. */
    if (code == HV_SOT || code == HV_EOC)
        return fail(cs, "tile-part header without SOD", pos);
    if (profile_full(cs) && !TileMarkerCode_Profile_IsConstraintValid(&profile_code, &err))
        return fail(cs, "tile.marker-code", pos);   /* TileMarkerCode-Profile */
    switch (code) {
    case HV_COD:
        if (cs->sot.tpsot != 0 || cs->tp_cod++)
            return fail(cs, "tile.cod-once", pos);
        if (DECODE(CodSegment, &cs->tile_cod, cs->buf, pos + MARKER, len) != 0)
            return fail(cs, "invalid COD", pos);
        if ((error = check_cod(cs, &cs->tile_cod.body)) != NULL)
            return fail(cs, error, pos);
        item->cod = &cs->tile_cod;
        break;
    case HV_QCD:
        if (cs->sot.tpsot != 0 || cs->tp_qcd++)
            return fail(cs, "tile.qcd-once", pos);
        if (DECODE(QcdSegment, &cs->tile_qcd, cs->buf, pos + MARKER, len) != 0)
            return fail(cs, "invalid QCD", pos);
        item->qcd = &cs->tile_qcd;
        break;
    case HV_PLT: {
        /* Zplt, then Iplt entries, at least one, to the end of the
         * segment: all of them decode before the rules apply, in the order
         * of the whole-file model's decoder (the Zplt sequence, then each
         * entry). HV_ACCEPT_PLT_PADDING takes zero entries after the last
         * packet of each tile-part; the profile, after the last packet of
         * the codestream (hv_rule_plt_entry). */
        int per_tile_part = (cs->flags & HV_ACCEPT_PLT_PADDING) != 0;
        size_t body = pos + MARKER + HV_FIXED(SegmentLength), used, p;
        uint64_t v;
        if (DECODE_USED(Zplt, &cs->plt.zplt, cs->buf, body, end - body, &used) != 0 ||
            body + used == end)
            return fail(cs, "invalid PLT", pos);
        cs->plt.start = body + used;
        cs->plt.end = end;
        for (p = cs->plt.start; p < end; ) {
            Iplt entry;
            if (DECODE_USED(Iplt, &entry, cs->buf, p, end - p, &used) != 0)
                return fail(cs, "invalid PLT", pos);
            p += used;
        }
        if (cs->plt.zplt != (asn1SccUint)cs->plts++)
            return fail(cs, "plt.zplt-sequence", pos);
        for (p = cs->plt.start; p < end; ) {
            if ((error = plt_entry(cs->buf, &p, end, &v)) != NULL)
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
        item->plt = &cs->plt;
        break;
    }
    case HV_COM:
        if (decode_com(cs, pos, end) != 0)
            return -1;
        item->com = &cs->com;
        break;
    case HV_COC: case HV_QCC: case HV_RGN: case HV_POC: case HV_PPT:
        break;
    case HV_SOC: case HV_SIZ: case HV_TLM: case HV_PLM: case HV_PPM: case HV_CRG: case HV_SOP:
    case HV_EPH:
        return fail(cs, "tile.marker-code", pos);
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

    memset(item, 0, sizeof *item);
    switch (cs->state) {
    case ST_DONE:
        return cs->error ? -1 : 0;

    case ST_SIZ:
        item->kind = HV_SEGMENT;
        item->code = HV_SIZ;
        item->start = cs->pos;
        item->end = cs->siz_end;
        item->siz = &cs->siz;
        cs->pos = item->end;
        cs->state = ST_MAIN;
        return 1;

    case ST_MAIN:
    case ST_AFTER_DATA:
        if (read_segment(cs, cs->end, &code, &end) != 0)
            return -1;
        if (code == HV_SOT)
            return start_tile_part(cs, item);
        if (code == HV_EOC) {
            if (cs->tile_parts == 0)
                return fail(cs, "codestream.no-tile-part", cs->pos);
            if (end != cs->end)
                return fail(cs, "bytes after EOC", end);
            {
                hv_tile_parts seen = {cs->tiles, cs->parts, cs->tnsot};
                if ((error = hv_rule_tile_parts_end(&seen)) != NULL)
                    return fail(cs, error, cs->pos);
            }
            if (profile_full(cs) &&
                (error = hv_rule_plt_packets(&cs->plt_count, &cs->siz, &cs->cod.body.sgcod,
                                             &cs->cod.body.spcod, 1)) != NULL)
                return fail(cs, error, cs->pos);
            item->kind = HV_END;
            item->code = HV_EOC;
            item->start = cs->pos;
            item->end = end;
            cs->pos = end;
            cs->state = ST_DONE;
            return 1;
        }
        if (cs->state == ST_AFTER_DATA)
            return fail(cs, "codestream.segment-after-sot", cs->pos);
        return main_segment(cs, code, end, item);

    case ST_TILE_HEADER:
        if (read_segment(cs, cs->tp_end, &code, &end) != 0)
            return -1;
        if (code == HV_SOD) {
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
