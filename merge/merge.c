/* merge.c: see merge.h. A port of hvJP2K's jpx_merge, box for box; the
 * JPX boxes the server reads (flst, url, dtbl) are written with the
 * encoders generated from the model, and every input and link is checked
 * with the reader's rules. */
#define _XOPEN_SOURCE 700

#include "merge.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "hv_codes.h"
#include "hv_error.h"
#include "hv_reader.h"
#include "hv_writer.h"
#include "jp2-boxes.h"

/* The Colour Specification boxes read from one jp2h. */
#define MAX_COLR 16

/* The codestream (1) and compositing layer (2) numbers of an nlst entry
 * (T.801 M.11.14.1): the association type in the top byte, the index in
 * the other 24 bits, which also bound the input count (to 16,777,215). */
enum { NLST_CODESTREAM = 0x01000000, NLST_LAYER = 0x02000000, NLST_INDEX_MAX = 0xFFFFFF };

/* The signature box's payload (T.800 I.5.1). */
static const uint8_t jp_signature[] = HV_SIGNATURE_BYTES;

/* One input: its boxes, type 0 where absent, and what the output needs of
 * them; in.buf only while the input is open. */
typedef struct {
    hv_merge_input in;
    hv_box jp2h, jp2c, xml;
    hv_box ihdr, bpcc, pclr, cmap, cdef, res;
    hv_box colr[MAX_COLR];
    int ncolr;
    unsigned rsiz, nc;
    int opacity, premultiplied;     /* cdef Typ 1, Typ 2 */
} source;

typedef struct {
    FILE *file;
    hv_out out;             /* boxes not yet written to file */
    uint64_t written;
    char *error;
    size_t error_size;
} writer;

/* The generated decoders and encoders on a byte array: size bytes to
 * decode, of which *used (unless NULL) were read, and a fixed-size type's
 * HV_FIXED(T) bytes to encode into. 0, or -1. */
#define DEFINE_DECODE(T)                                                     \
    static int decode_##T(T *value, const uint8_t *bytes, size_t size,      \
                          size_t *used) {                                    \
        BitStream s;                                                         \
        int err = 0;                                                         \
        BitStream_AttachBuffer(&s, (unsigned char *)bytes, (long)size);     \
        if (!T##_ACN_Decode(value, &s, &err))                               \
            return -1;                                                       \
        if (used != NULL)                                                    \
            *used = (size_t)BitStream_GetLength(&s);                         \
        return 0;                                                            \
    }
#define DEFINE_ENCODE(T)                                                     \
    static int encode_##T(const T *value, uint8_t bytes[HV_FIXED(T)]) {     \
        BitStream s;                                                         \
        int err = 0;                                                         \
        memset(bytes, 0, HV_FIXED(T));      /* the encoders skip 0 bits */   \
        BitStream_AttachBuffer(&s, bytes, (long)HV_FIXED(T));               \
        return T##_ACN_Encode(value, &s, &err, TRUE) ? 0 : -1;              \
    }

DEFINE_DECODE(Ihdr)
DEFINE_DECODE(ColrHeader)
DEFINE_DECODE(CdefCount)
DEFINE_DECODE(CdefEntry)
DEFINE_ENCODE(CmapEntry)
DEFINE_ENCODE(FtypHeader)
DEFINE_ENCODE(Brand)

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* ------------------------------------------------------------------------
 * Inputs
 * ------------------------------------------------------------------------ */

/* The Rsiz of a codestream hv_codestream_check accepted. */
static unsigned codestream_rsiz(const uint8_t *buf, const hv_box *jp2c) {
    hv_codestream cs;
    unsigned rsiz = 0;
    if (hv_codestream_open(&cs, buf, jp2c->payload, jp2c->end, HV_PROFILE) == 0)
        rsiz = (unsigned)hv_codestream_siz(&cs)->rsiz;
    hv_codestream_close(&cs);
    return rsiz;
}

/* Where s records the first jp2h child of this type, or NULL. */
static hv_box *header_box(source *s, uint32_t type) {
    switch (type) {
    case HV_BOX_IHDR: return &s->ihdr;
    case HV_BOX_BPCC: return &s->bpcc;
    case HV_BOX_PCLR: return &s->pclr;
    case HV_BOX_CMAP: return &s->cmap;
    case HV_BOX_CDEF: return &s->cdef;
    case HV_BOX_RES: return &s->res;
    default: return NULL;
    }
}

static int read_source(const hv_merge_input *in, source *s, char *error, size_t size) {
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at;
    int status;
    Ihdr ihdr;

    memset(s, 0, sizeof *s);
    s->in = *in;
    if ((message = hv_check_jp2(in->buf, in->size, &s->jp2c, &at)) != NULL ||
        (message = hv_codestream_check(in->buf, s->jp2c.payload, s->jp2c.end, HV_PROFILE, &at)) !=
            NULL ||
        (message = hv_check_jp2h(in->buf, in->size, &at)) != NULL)
        return hv_fail(error, size, "%s: %s at %zu", in->path, message, at);
    hv_boxes_file(&it, in->buf, in->size);
    while (hv_boxes_next(&it, &box, &message, &at) == 1) {
        if (box.type == HV_BOX_JP2H && s->jp2h.type == 0)
            s->jp2h = box;
        else if (box.type == HV_BOX_XML && s->xml.type == 0)
            s->xml = box;
    }
    /* hv_check_jp2h: one jp2h, ihdr first and NC = Csiz (at most 16,384),
     * at most one bpcc, pclr, cmap, cdef and res, each decoded by the
     * model's types. */
    hv_boxes_children(&it, in->buf, &s->jp2h);
    while ((status = hv_boxes_next(&it, &box, &message, &at)) == 1) {
        hv_box *first = header_box(s, box.type);
        if (first != NULL && first->type == 0)
            *first = box;
        if (box.type == HV_BOX_COLR) {
            if (s->ncolr == MAX_COLR)
                return hv_fail(error, size, "%s: more than %d Colour Specification boxes",
                               in->path, MAX_COLR);
            s->colr[s->ncolr++] = box;
        }
    }
    if (status < 0)
        return hv_fail(error, size, "%s: %s at %zu", in->path, message, at);
    s->rsiz = codestream_rsiz(in->buf, &s->jp2c);
    if (decode_Ihdr(&ihdr, in->buf + s->ihdr.payload, s->ihdr.end - s->ihdr.payload, NULL) != 0)
        return hv_fail(error, size, "%s: ihdr at %zu cannot be decoded", in->path, s->ihdr.start);
    s->nc = (unsigned)ihdr.nc;
    if (s->cdef.type != 0) {
        /* N, then N (Cn, Typ, Asoc), as hv_check_jp2h decoded them. */
        const uint8_t *p = in->buf + s->cdef.payload;
        size_t n = s->cdef.end - s->cdef.payload, used = HV_FIXED(CdefCount), c;
        CdefCount count;
        CdefEntry entry;
        if (decode_CdefCount(&count, p, n, NULL) != 0)
            return hv_fail(error, size, "%s: cdef at %zu cannot be decoded", in->path,
                           s->cdef.start);
        for (c = 0; c < count; c++, used += HV_FIXED(CdefEntry)) {
            if (n - used < HV_FIXED(CdefEntry) ||
                decode_CdefEntry(&entry, p + used, HV_FIXED(CdefEntry), NULL) != 0)
                return hv_fail(error, size, "%s: cdef at %zu cannot be decoded", in->path,
                               s->cdef.start);
            s->opacity |= entry.typ == 1;
            s->premultiplied |= entry.typ == 2;
        }
    }
    return 0;
}

/* The first input's Colour Specification boxes as the JPX file's jp2h
 * holds them (hv_rule_colr for a JPX jp2h: at most one with METH 1 and one
 * with METH 2, T.801 M.11.7.1), which a JP2 file's jp2h need not keep. */
static int check_jpx_colrs(const source *s, char *error, size_t size) {
    hv_header *h = malloc(sizeof *h);
    const char *rule = NULL;
    size_t used = 0;
    int k;
    if (h == NULL)
        return hv_fail(error, size, "out of memory");
    hv_header_init(h, HV_BOX_JP2H, 1);
    for (k = 0; k < s->ncolr && rule == NULL; k++) {
        const hv_box *colr = &s->colr[k];
        ColrHeader header;
        if (decode_ColrHeader(&header, s->in.buf + colr->payload, colr->end - colr->payload,
                              &used) != 0) {
            free(h);
            return hv_fail(error, size, "%s: colr at %zu cannot be decoded", s->in.path,
                           colr->start);
        }
        rule = hv_rule_colr(h, &header, colr->end - colr->payload - used);
    }
    free(h);
    return rule == NULL ? 0
                        : hv_fail(error, size, "%s: %s at %zu, in the JPX file's jp2h",
                                  s->in.path, rule, s->colr[k - 1].start);
}

static int same_box_location(const hv_box *a, const hv_box *b) {
    return a->type == b->type && a->start == b->start && a->payload == b->payload &&
           a->end == b->end && a->to_end == b->to_end;
}

/* Every pass-1 value used to write the output, excluding the input mapping. */
static int same_source_record(const source *a, const source *b) {
    int i;
    if (!same_box_location(&a->jp2h, &b->jp2h) ||
        !same_box_location(&a->jp2c, &b->jp2c) ||
        !same_box_location(&a->xml, &b->xml) ||
        !same_box_location(&a->ihdr, &b->ihdr) ||
        !same_box_location(&a->bpcc, &b->bpcc) ||
        !same_box_location(&a->pclr, &b->pclr) ||
        !same_box_location(&a->cmap, &b->cmap) ||
        !same_box_location(&a->cdef, &b->cdef) ||
        !same_box_location(&a->res, &b->res) ||
        a->ncolr != b->ncolr || a->rsiz != b->rsiz || a->nc != b->nc ||
        a->opacity != b->opacity || a->premultiplied != b->premultiplied)
        return 0;
    for (i = 0; i < a->ncolr; i++)
        if (!same_box_location(&a->colr[i], &b->colr[i]))
            return 0;
    return 1;
}

/* The same contents, or both absent. */
static int same_box(const source *a, const hv_box *x, const source *b, const hv_box *y) {
    size_t n = x->end - x->payload;
    if (x->type == 0 || y->type == 0)
        return x->type == y->type;
    return x->type == y->type && n == y->end - y->payload &&
           memcmp(a->in.buf + x->payload, b->in.buf + y->payload, n) == 0;
}

/* Byte i of a Colour Specification box's payload as the JPX file carries
 * it. Its APPROX, after METH and PREC (T.800 I.5.3.3): T.801 M.11.7.2 has
 * no 0, which JP2 files write, so 0 becomes 1 (as hvJP2K's jpx_colr). */
enum { COLR_APPROX = 2 };

static uint8_t colr_byte(const uint8_t *payload, size_t i) {
    return i == COLR_APPROX && payload[i] == 0 ? 1 : payload[i];
}

static int same_colrs(const source *a, const source *b) {
    int k;
    size_t i, n;
    if (a->ncolr != b->ncolr)
        return 0;
    for (k = 0; k < a->ncolr; k++) {
        const uint8_t *x = a->in.buf + a->colr[k].payload, *y = b->in.buf + b->colr[k].payload;
        n = a->colr[k].end - a->colr[k].payload;
        if (n != b->colr[k].end - b->colr[k].payload)
            return 0;
        for (i = 0; i < n; i++)
            if (colr_byte(x, i) != colr_byte(y, i))
                return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * Output
 * ------------------------------------------------------------------------ */

/* The boxes in w->out, written to the file. */
static int flush(writer *w) {
    if (w->out.error != NULL)
        return hv_fail(w->error, w->error_size, "%s", w->out.error);
    if (w->out.size > INT_MAX - w->written)
        return hv_fail(w->error, w->error_size,
                       "file.size-limit: output larger than INT_MAX bytes");
    if (w->out.size != 0 && fwrite(w->out.data, 1, w->out.size, w->file) != w->out.size)
        return hv_fail(w->error, w->error_size, "cannot write the JPX file");
    w->written += w->out.size;
    hv_out_rewind(&w->out, 0);
    return 0;
}

/* Bytes of an input, written straight to the file after w->out. */
static int write_through(writer *w, const uint8_t *bytes, size_t n) {
    if (flush(w) != 0)
        return -1;
    if (n > INT_MAX - w->written)
        return hv_fail(w->error, w->error_size,
                       "file.size-limit: output larger than INT_MAX bytes");
    if (n != 0 && fwrite(bytes, 1, n, w->file) != n)
        return hv_fail(w->error, w->error_size, "cannot write the JPX file");
    w->written += n;
    return 0;
}

/* The signature and File Type boxes (T.800 I.5.1, T.801 M.11.1.2), as
 * hvJP2K writes them: brand jpx, MinV 1, compatible with jpx, and when the
 * codestreams are embedded with jp2 and jpxb too. */
static int write_start(hv_out *out, int links) {
    static const Brand embedded[] = {HV_BRAND_JPX, HV_BRAND_JP2, HV_BRAND_JPXB};
    static const Brand linked[] = {HV_BRAND_JPX};
    const Brand *compatible = links ? linked : embedded;
    size_t ncompatible = links ? sizeof linked / sizeof *linked
                               : sizeof embedded / sizeof *embedded, i, start;
    FtypHeader header = {HV_BRAND_JPX, 1};
    uint8_t bytes[HV_FIXED(FtypHeader)];

    if (hv_write_box_header(out, HV_BOX_JP, sizeof jp_signature) != 0 ||
        hv_write_bytes(out, jp_signature, sizeof jp_signature) != 0 ||
        hv_begin_box(out, HV_BOX_FTYP, 0, &start) != 0)
        return -1;
    if (encode_FtypHeader(&header, bytes) != 0 || hv_write_bytes(out, bytes, sizeof bytes) != 0)
        return -1;
    for (i = 0; i < ncompatible; i++)
        if (encode_Brand(&compatible[i], bytes) != 0 ||
            hv_write_bytes(out, bytes, HV_FIXED(Brand)) != 0)
            return -1;
    return hv_end_box(out, start);
}

/* A box of an input with a fresh header, as glymur writes it. */
static int copy_box(hv_out *out, const source *s, const hv_box *box) {
    return hv_write_box_header(out, box->type, box->end - box->payload) != 0 ? -1
         : hv_write_bytes(out, s->in.buf + box->payload, box->end - box->payload);
}

static int copy_colr(hv_out *out, const source *s, const hv_box *box) {
    size_t n = box->end - box->payload, i;
    const uint8_t *p = s->in.buf + box->payload;
    uint8_t byte;
    if (hv_write_box_header(out, HV_BOX_COLR, n) != 0)
        return -1;
    for (i = 0; i < n; i++) {
        byte = colr_byte(p, i);
        if (hv_write_bytes(out, &byte, 1) != 0)
            return -1;
    }
    return 0;
}

/* The JP2 Header box of the first input, with the APPROX fixed. */
static int write_jp2h(hv_out *out, const source *s) {
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at, start;
    if (hv_begin_box(out, HV_BOX_JP2H, 0, &start) != 0)
        return -1;
    hv_boxes_children(&it, s->in.buf, &s->jp2h);
    while (hv_boxes_next(&it, &box, &message, &at) == 1)
        if ((box.type == HV_BOX_COLR ? copy_colr(out, s, &box) : copy_box(out, s, &box)) != 0)
            return -1;
    return hv_end_box(out, start);
}

/* Entry i of the Component Mapping for an input without the palette of
 * the first: component i used directly (CMP = i, MTYP = 0, PCOL = 0). */
static int cmap_entry(unsigned i, uint8_t bytes[HV_FIXED(CmapEntry)]) {
    CmapEntry entry = {i, 0, 0};
    return encode_CmapEntry(&entry, bytes);
}

/* That Component Mapping box, unless the first input's cmap is the same:
 * one entry per component, NC of them (the checked Csiz, at most 16,384). */
static int write_generated_cmap(hv_out *out, const source *s, const source *first) {
    const hv_box *c0 = &first->cmap;
    const uint8_t *p0 = first->in.buf + c0->payload;
    size_t n = HV_FIXED(CmapEntry) * (size_t)s->nc;
    uint8_t bytes[HV_FIXED(CmapEntry)];
    unsigned i;
    int same = c0->type != 0 && c0->end - c0->payload == n;

    for (i = 0; same && i < s->nc; i++)
        same = cmap_entry(i, bytes) == 0 &&
               memcmp(bytes, p0 + HV_FIXED(CmapEntry) * i, sizeof bytes) == 0;
    if (same)
        return 0;
    if (hv_write_box_header(out, HV_BOX_CMAP, n) != 0)
        return -1;
    for (i = 0; i < s->nc; i++)
        if (cmap_entry(i, bytes) != 0 || hv_write_bytes(out, bytes, sizeof bytes) != 0)
            return -1;
    return 0;
}

/* Codestream and Compositing Layer Header boxes: empty where the input's
 * JP2 Header box is the first input's, otherwise its Image Header and what
 * differs from the first (hvJP2K's write_jpch_jplh). */
static int write_headers(hv_out *out, const source *s, const source *first) {
    size_t start, group;
    const hv_box *j = &s->jp2h, *j0 = &first->jp2h;

    if (j->end - j->start == j0->end - j0->start &&
        memcmp(s->in.buf + j->start, first->in.buf + j0->start, j->end - j->start) == 0)
        return hv_write_box_header(out, HV_BOX_JPCH, 0) != 0 ? -1
             : hv_write_box_header(out, HV_BOX_JPLH, 0);

    if (hv_begin_box(out, HV_BOX_JPCH, 0, &start) != 0 || copy_box(out, s, &s->ihdr) != 0)
        return -1;
    if (s->bpcc.type != 0 && !same_box(s, &s->bpcc, first, &first->bpcc) &&
        copy_box(out, s, &s->bpcc) != 0)
        return -1;
    if (s->pclr.type != 0 && !same_box(s, &s->pclr, first, &first->pclr) &&
        copy_box(out, s, &s->pclr) != 0)
        return -1;
    if (s->pclr.type == 0 && first->pclr.type != 0) {
        if (write_generated_cmap(out, s, first) != 0)
            return -1;
    } else if (s->cmap.type != 0 && !same_box(s, &s->cmap, first, &first->cmap) &&
               copy_box(out, s, &s->cmap) != 0) {
        return -1;
    }
    if (hv_end_box(out, start) != 0 || hv_begin_box(out, HV_BOX_JPLH, 0, &start) != 0)
        return -1;
    if (!same_colrs(s, first)) {
        int k;
        if (hv_begin_box(out, HV_BOX_CGRP, 0, &group) != 0)
            return -1;
        for (k = 0; k < s->ncolr; k++)
            if (copy_colr(out, s, &s->colr[k]) != 0)
                return -1;
        if (hv_end_box(out, group) != 0)
            return -1;
    }
    if (s->cdef.type != 0 && !same_box(s, &s->cdef, first, &first->cdef) &&
        copy_box(out, s, &s->cdef) != 0)
        return -1;
    if (s->res.type != 0 && !same_box(s, &s->res, first, &first->res) &&
        copy_box(out, s, &s->res) != 0)
        return -1;
    return hv_end_box(out, start);
}

/* Reader Requirements (T.801 M.11.1), as hvJP2K's reader_requirements:
 * standard features 1 (no extensions beyond the JP2 features), 2 (more
 * than one codestream), 4 and 5 (the codestreams' Rsiz), 9 and 10 (opacity
 * channels in cdef), 15 (linked codestreams), each needed for both the
 * Fully Understand and the Display expressions: feature i sets mask bit i,
 * FUAM and DCM all of them. At most 7 features, so ML is 1. 0, or -1 with
 * a message in w->error, or with w->out.error set. */
static int write_rreq(writer *w, const source *s, size_t n, int links) {
    int has[16] = {0}, k = 0, i, status;
    Rreq_Std *rreq = calloc(1, sizeof *rreq);
    size_t j;

    if (rreq == NULL)
        return hv_fail(w->error, w->error_size, "out of memory");
    has[1] = 1;
    for (j = 0; j < n; j++) {
        has[9] |= s[j].opacity;
        has[10] |= s[j].premultiplied;
        has[4] |= s[j].rsiz == 2;
        has[5] |= s[j].rsiz != 1 && s[j].rsiz != 2;
    }
    has[2] = n > 1;
    has[15] = links != 0;
    rreq->fuam.nCount = rreq->dcm.nCount = 1;
    for (i = 0; i < 16; i++) {
        if (!has[i])
            continue;
        rreq->standard.arr[k].sf = (asn1SccUint)i;
        rreq->standard.arr[k].sm.nCount = 1;
        rreq->standard.arr[k].sm.arr[0] = (byte)(0x80 >> k);
        rreq->fuam.arr[0] |= (byte)(0x80 >> k);
        k++;
    }
    rreq->dcm.arr[0] = rreq->fuam.arr[0];
    rreq->standard.nCount = k;
    rreq->vendor.nCount = 0;
    status = hv_write_rreq(&w->out, rreq);
    free(rreq);
    return status;
}

/* The XML box of input i as read, associated with codestream i and
 * compositing layer i by an asoc holding an nlst of the two. An XML box
 * running to the end of its file gets an explicit length. The asoc's
 * length is known before its XML box, which may be copied straight to the
 * file. */
static int write_xml(writer *w, const source *s, size_t i) {
    const hv_box *xml = &s->xml;
    size_t payload = xml->end - xml->payload,
           box = xml->to_end ? HV_BOX_HEADER + payload : xml->end - xml->start;
    uint8_t nlst[8];
    put32(nlst, NLST_CODESTREAM + (uint32_t)i);
    put32(nlst + 4, NLST_LAYER + (uint32_t)i);
    if (hv_write_box_header(&w->out, HV_BOX_ASOC, HV_BOX_HEADER + sizeof nlst + (uint64_t)box) !=
            0 ||
        hv_write_box_header(&w->out, HV_BOX_NLST, sizeof nlst) != 0 ||
        hv_write_bytes(&w->out, nlst, sizeof nlst) != 0)
        return -1;
    return xml->to_end ? copy_box(&w->out, s, xml)
                       : write_through(w, s->in.buf + xml->start, box);
}

/* The URL of a linked file: file:// and its absolute path, resolved, with
 * the bytes Python's urllib.parse.quote_from_bytes escapes (as pathlib's
 * as_uri, which hvJP2K uses). NULL, with a message in error. */
static char *link_url(const char *path, char *error, size_t size) {
    static const char hex[] = "0123456789ABCDEF", scheme[] = "file://";
    char *real = realpath(path, NULL), *url, *p;
    const char *q;
    if (real == NULL) {
        hv_fail(error, size, "%s: cannot resolve the path", path);
        return NULL;
    }
    if ((url = malloc(sizeof scheme + 3 * strlen(real))) == NULL) {
        free(real);
        hv_fail(error, size, "out of memory");
        return NULL;
    }
    memcpy(url, scheme, sizeof scheme - 1);
    for (p = url + sizeof scheme - 1, q = real; *q; q++) {
        unsigned char c = (unsigned char)*q;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '.' || c == '-' || c == '~' || c == '/') {
            *p++ = (char)c;
        } else {
            *p++ = '%';
            *p++ = hex[c >> 4];
            *p++ = hex[c & 15];
        }
    }
    *p = 0;
    free(real);
    return url;
}

/* Opens input i. First (again = 0): reads it into *s, where it stays open.
 * Again: checks it against the first-pass record in *s (its size and
 * everything read_source records), whose mapping it replaces. On failure
 * the input is closed again and *s keeps no mapping. */
static int open_input(const hv_merge_inputs *inputs, size_t i, source *s, int again,
                      char *error, size_t error_size) {
    hv_merge_input in;
    source read;
    int bad;
    memset(&in, 0, sizeof in);
    if (inputs->open(inputs->context, i, &in, error, error_size) != 0) {
        s->in.buf = NULL;
        return -1;
    }
    bad = (again && in.size != s->in.size) || read_source(&in, &read, error, error_size) != 0 ||
          (again && !same_source_record(s, &read));
    if (bad) {
        inputs->close(inputs->context, i, &in);
        s->in.buf = NULL;
        return again ? hv_fail(error, error_size, "%s: changed while merging", s->in.path) : -1;
    }
    if (again)
        s->in = in;
    else
        *s = read;
    return 0;
}

static void close_input(const hv_merge_inputs *inputs, size_t i, source *s) {
    if (s->in.buf != NULL)
        inputs->close(inputs->context, i, &s->in);
    s->in.buf = NULL;
}

int hv_merge_files(const hv_merge_inputs *inputs, size_t n, int links, FILE *file, char *error,
                   size_t error_size) {
    writer w;
    source *s;
    char **urls = NULL;
    size_t i, start;
    int status = -1;

    memset(&w, 0, sizeof w);
    w.file = file;
    w.error = error;
    w.error_size = error_size;
    error[0] = 0;
    if (n == 0)
        return hv_fail(error, error_size, "no JP2 input files");
    if (links && n > UINT16_MAX)
        return hv_fail(error, error_size, "a linked JPX file holds at most 65,535 links");
    if (n > NLST_INDEX_MAX)
        return hv_fail(error, error_size, "a JPX file holds at most 16,777,215 input files");
    if ((s = calloc(n, sizeof *s)) == NULL || (links && (urls = calloc(n, sizeof *urls)) == NULL)) {
        free(s);
        return hv_fail(error, error_size, "out of memory");
    }
    hv_out_init(&w.out);

    /* First pass: every input checked and what the output needs of it
     * recorded; opened one at a time, besides the first, which every later
     * one is compared with and which stays open. */
    for (i = 0; i < n; i++) {
        if (open_input(inputs, i, &s[i], 0, error, error_size) != 0)
            goto done;
        if (i > 0)
            close_input(inputs, i, &s[i]);
    }
    if (check_jpx_colrs(&s[0], error, error_size) != 0)
        goto done;
    /* A jplh without a cdef or res box takes the one of jp2h, the first
     * input's (T.801 M.11.7): an input without one cannot follow a first
     * input with one. hvJP2K writes such a file. */
    for (i = 1; i < n; i++) {
        const char *missing = s[0].cdef.type != 0 && s[i].cdef.type == 0 ? "cdef"
                            : s[0].res.type != 0 && s[i].res.type == 0   ? "res"
                                                                         : NULL;
        if (missing != NULL) {
            hv_fail(error, error_size, "%s: no %s box, but the first input has one, which it "
                    "would inherit", s[i].in.path, missing);
            goto done;
        }
    }
    for (i = 0; links && i < n; i++) {
        const char *rule;
        if ((urls[i] = link_url(s[i].in.path, error, error_size)) == NULL)
            goto done;
        if ((rule = hv_rule_url(0, 0, (const uint8_t *)urls[i], strlen(urls[i]) + 1)) != NULL) {
            hv_fail(error, error_size, "%s: %s", s[i].in.path, rule);
            goto done;
        }
    }

    /* Second pass: the output, each later input open again while its
     * header and XML boxes, and its codestream unless linked, are copied. */
    if (write_start(&w.out, links) != 0 || write_rreq(&w, s, n, links) != 0 ||
        write_jp2h(&w.out, &s[0]) != 0 || flush(&w) != 0)
        goto done;
    for (i = 0; i < n; i++) {
        const hv_box *cs = &s[i].jp2c;
        if (i > 0 && open_input(inputs, i, &s[i], 1, error, error_size) != 0)
            goto done;
        if (write_headers(&w.out, &s[i], &s[0]) != 0)
            goto done;
        if (links) {
            if (hv_begin_box(&w.out, HV_BOX_FTBL, 0, &start) != 0 ||
                hv_write_flst(&w.out, cs->payload, (uint32_t)(cs->end - cs->payload),
                              (uint16_t)(i + 1)) != 0 ||
                hv_end_box(&w.out, start) != 0)
                goto done;
        } else if (hv_write_box_header(&w.out, HV_BOX_JP2C, cs->end - cs->payload) != 0 ||
                   write_through(&w, s[i].in.buf + cs->payload, cs->end - cs->payload) != 0) {
            goto done;
        }
        if (s[i].xml.type != 0 && write_xml(&w, &s[i], i) != 0)
            goto done;
        if (flush(&w) != 0)
            goto done;
        if (i > 0)
            close_input(inputs, i, &s[i]);
    }
    if (links) {
        if (hv_begin_box(&w.out, HV_BOX_DTBL, 0, &start) != 0 ||
            hv_write_ndr(&w.out, (uint16_t)n) != 0)
            goto done;
        for (i = 0; i < n; i++)
            if (hv_write_url(&w.out, urls[i]) != 0)
                goto done;
        if (hv_end_box(&w.out, start) != 0)
            goto done;
    }
    if (flush(&w) != 0 || fflush(file) != 0) {
        if (error[0] == 0)
            hv_fail(error, error_size, "cannot write the JPX file");
        goto done;
    }
    status = 0;

done:
    if (status != 0 && error[0] == 0)
        hv_fail(error, error_size, "%s", w.out.error ? w.out.error : "out of memory");
    for (i = 0; i < n; i++)
        close_input(inputs, i, &s[i]);
    for (i = 0; urls != NULL && i < n; i++)
        free(urls[i]);
    free(urls);
    free(s);
    hv_out_free(&w.out);
    return status;
}

/* Inputs already in memory. */
static int buffer_open(void *context, size_t i, hv_merge_input *in, char *error,
                       size_t error_size) {
    (void)error;
    (void)error_size;
    *in = ((const hv_merge_input *)context)[i];
    return 0;
}

static void buffer_close(void *context, size_t i, hv_merge_input *in) {
    (void)context;
    (void)i;
    (void)in;
}

int hv_merge_buffers(const hv_merge_input *inputs, size_t n, int links, FILE *file, char *error,
                     size_t error_size) {
    hv_merge_inputs from = {buffer_open, buffer_close, NULL};
    from.context = (void *)inputs;
    return hv_merge_files(&from, n, links, file, error, error_size);
}
