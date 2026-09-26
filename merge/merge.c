/* merge.c: see merge.h. A port of hvJP2K's jpx_merge, box for box; the
 * JPX boxes the server reads (flst, url, dtbl) are written with the
 * encoders generated from the model, and every input and link is checked
 * with the reader's rules. */
#define _XOPEN_SOURCE 700

#include "merge.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "hv_reader.h"
#include "hv_writer.h"

enum {
    BOX_JP2H = 0x6A703268, BOX_XML = 0x786D6C20, BOX_IHDR = 0x69686472, BOX_BPCC = 0x62706363,
    BOX_COLR = 0x636F6C72, BOX_PCLR = 0x70636C72, BOX_CMAP = 0x636D6170, BOX_CDEF = 0x63646566,
    BOX_RES = 0x72657320, BOX_JPLH = 0x6A706C68, BOX_CGRP = 0x63677270, BOX_ASOC = 0x61736F63,
    BOX_NLST = 0x6E6C7374, BOX_RREQ = 0x72726571, BOX_FTYP = 0x66747970
};

#define MAX_COLR 16

/* One input: its boxes, type 0 where absent. */
typedef struct {
    const hv_merge_input *in;
    hv_box jp2h, jp2c, xml;
    hv_box ihdr, bpcc, pclr, cmap, cdef, res;
    hv_box colr[MAX_COLR];
    int ncolr;
    unsigned rsiz;
} source;

typedef struct {
    FILE *file;
    hv_out out;             /* boxes not yet written to file */
    uint64_t written;
    char *error;
    size_t error_size;
} writer;

static int fail(char *error, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
    return -1;
}

static unsigned get16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* ------------------------------------------------------------------------
 * Inputs
 * ------------------------------------------------------------------------ */

static int read_source(const hv_merge_input *in, source *s, char *error, size_t size) {
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at;
    int status;

    memset(s, 0, sizeof *s);
    s->in = in;
    if ((message = hv_check_jp2(in->buf, in->size, &s->jp2c, &at)) != NULL ||
        (message = hv_codestream_check(in->buf, s->jp2c.payload, s->jp2c.end, HV_PROFILE, &at)) !=
            NULL ||
        (message = hv_check_jp2h(in->buf, in->size, &at)) != NULL)
        return fail(error, size, "%s: %s at %zu", in->path, message, at);
    hv_boxes_file(&it, in->buf, in->size);
    while (hv_boxes_next(&it, &box, &message, &at) == 1) {
        if (box.type == BOX_JP2H && s->jp2h.type == 0)
            s->jp2h = box;
        else if (box.type == BOX_XML && s->xml.type == 0)
            s->xml = box;
    }
    /* hv_check_jp2h: one jp2h, ihdr first and NC = Csiz (at most 16 384),
     * at most one bpcc, pclr, cmap, cdef and res. */
    hv_boxes_children(&it, in->buf, &s->jp2h);
    while ((status = hv_boxes_next(&it, &box, &message, &at)) == 1) {
        hv_box *first = box.type == BOX_IHDR ? &s->ihdr : box.type == BOX_BPCC ? &s->bpcc
                      : box.type == BOX_PCLR ? &s->pclr : box.type == BOX_CMAP ? &s->cmap
                      : box.type == BOX_CDEF ? &s->cdef : box.type == BOX_RES ? &s->res : NULL;
        if (first != NULL && first->type == 0)
            *first = box;
        if (box.type == BOX_COLR) {
            if (s->ncolr == MAX_COLR)
                return fail(error, size, "%s: more than %d Colour Specification boxes", in->path,
                            MAX_COLR);
            s->colr[s->ncolr++] = box;
        }
    }
    if (status < 0)
        return fail(error, size, "%s: %s at %zu", in->path, message, at);
    s->rsiz = get16(in->buf + s->jp2c.payload + 6);     /* SOC, SIZ, Lsiz, Rsiz */
    return 0;
}

/* The same contents, or both absent. */
static int same_box(const source *a, const hv_box *x, const source *b, const hv_box *y) {
    size_t n = x->end - x->payload;
    if (x->type == 0 || y->type == 0)
        return x->type == y->type;
    return x->type == y->type && n == y->end - y->payload &&
           memcmp(a->in->buf + x->payload, b->in->buf + y->payload, n) == 0;
}

/* The APPROX of a Colour Specification box as the JPX file carries it:
 * T.801 M.11.7.2 has no 0, which JP2 files write (as hvJP2K's jpx_colr). */
static uint8_t colr_byte(const uint8_t *payload, size_t i) {
    return i == 2 && payload[2] == 0 ? 1 : payload[i];
}

static int same_colrs(const source *a, const source *b) {
    int k;
    size_t i, n;
    if (a->ncolr != b->ncolr)
        return 0;
    for (k = 0; k < a->ncolr; k++) {
        const uint8_t *x = a->in->buf + a->colr[k].payload, *y = b->in->buf + b->colr[k].payload;
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

static int flush(writer *w) {
    if (w->out.error != NULL)
        return fail(w->error, w->error_size, "%s", w->out.error);
    if (w->out.size != 0 && fwrite(w->out.data, 1, w->out.size, w->file) != w->out.size)
        return fail(w->error, w->error_size, "cannot write the JPX file");
    w->written += w->out.size;
    w->out.size = 0;
    if (w->written > INT_MAX)
        return fail(w->error, w->error_size, "file.size-limit: output larger than INT_MAX bytes");
    return 0;
}

/* Bytes of an input, written straight to the file. */
static int write_through(writer *w, const uint8_t *bytes, size_t n) {
    if (flush(w) != 0)
        return -1;
    if (n > INT_MAX || w->written + n > INT_MAX)
        return fail(w->error, w->error_size, "file.size-limit: output larger than INT_MAX bytes");
    if (n != 0 && fwrite(bytes, 1, n, w->file) != n)
        return fail(w->error, w->error_size, "cannot write the JPX file");
    w->written += n;
    return 0;
}

/* A box of an input with a fresh header, as glymur writes it. */
static int copy_box(hv_out *out, const source *s, const hv_box *box) {
    return hv_write_box_header(out, box->type, box->end - box->payload) != 0 ? -1
         : hv_write_bytes(out, s->in->buf + box->payload, box->end - box->payload);
}

static int copy_colr(hv_out *out, const source *s, const hv_box *box) {
    size_t n = box->end - box->payload, i;
    const uint8_t *p = s->in->buf + box->payload;
    uint8_t byte;
    if (hv_write_box_header(out, BOX_COLR, n) != 0)
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
    if (hv_begin_box(out, BOX_JP2H, 0, &start) != 0)
        return -1;
    hv_boxes_children(&it, s->in->buf, &s->jp2h);
    while (hv_boxes_next(&it, &box, &message, &at) == 1)
        if ((box.type == BOX_COLR ? copy_colr(out, s, &box) : copy_box(out, s, &box)) != 0)
            return -1;
    return hv_end_box(out, start);
}

/* Component Mapping for an input without the palette of the first: every
 * component used directly (CMP = i, MTYP = 0, PCOL = 0). NC is the checked
 * Csiz, at most 16 384. */
static int generated_cmap(const source *s, uint8_t *payload, size_t *n) {
    unsigned nc = get16(s->in->buf + s->ihdr.payload + 8), i;
    for (i = 0; i < nc; i++) {
        payload[4 * i] = (uint8_t)(i >> 8);
        payload[4 * i + 1] = (uint8_t)i;
        payload[4 * i + 2] = payload[4 * i + 3] = 0;
    }
    *n = 4 * (size_t)nc;
    return 0;
}

/* Codestream and Compositing Layer Header boxes: empty where the input's
 * JP2 Header box is the first input's, otherwise its Image Header and what
 * differs from the first (hvJP2K's write_jpch_jplh). */
static int write_headers(hv_out *out, const source *s, const source *first) {
    size_t start, group;
    const hv_box *j = &s->jp2h, *j0 = &first->jp2h;

    if (j->end - j->start == j0->end - j0->start &&
        memcmp(s->in->buf + j->start, first->in->buf + j0->start, j->end - j->start) == 0)
        return hv_write_box_header(out, HV_BOX_JPCH, 0) != 0 ? -1
             : hv_write_box_header(out, BOX_JPLH, 0);

    if (hv_begin_box(out, HV_BOX_JPCH, 0, &start) != 0 || copy_box(out, s, &s->ihdr) != 0)
        return -1;
    if (s->bpcc.type != 0 && !same_box(s, &s->bpcc, first, &first->bpcc) &&
        copy_box(out, s, &s->bpcc) != 0)
        return -1;
    if (s->pclr.type != 0 && !same_box(s, &s->pclr, first, &first->pclr) &&
        copy_box(out, s, &s->pclr) != 0)
        return -1;
    if (s->pclr.type == 0 && first->pclr.type != 0) {
        static uint8_t cmap[4 * 16384];
        size_t n;
        generated_cmap(s, cmap, &n);
        if ((first->cmap.type == 0 || n != first->cmap.end - first->cmap.payload ||
             memcmp(cmap, first->in->buf + first->cmap.payload, n) != 0) &&
            (hv_write_box_header(out, BOX_CMAP, n) != 0 || hv_write_bytes(out, cmap, n) != 0))
            return -1;
    } else if (s->cmap.type != 0 && !same_box(s, &s->cmap, first, &first->cmap) &&
               copy_box(out, s, &s->cmap) != 0) {
        return -1;
    }
    if (hv_end_box(out, start) != 0 || hv_begin_box(out, BOX_JPLH, 0, &start) != 0)
        return -1;
    if (!same_colrs(s, first)) {
        int k;
        if (hv_begin_box(out, BOX_CGRP, 0, &group) != 0)
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
 * Fully Understand and the Display expressions. */
static int write_rreq(hv_out *out, const source *s, size_t n, int links) {
    int has[16] = {0}, features[16], k = 0, ml, i;
    uint8_t box[8 + 1 + 2 * 2 + 2 + 16 * (2 + 2) + 2];
    size_t len = 8, j, c;

    has[1] = 1;
    for (j = 0; j < n; j++) {
        const uint8_t *cdef = s[j].in->buf + s[j].cdef.payload;
        size_t m = s[j].cdef.type ? (s[j].cdef.end - s[j].cdef.payload) : 0;
        for (c = 0; m >= 2 && c < get16(cdef) && 2 + 6 * c + 6 <= m; c++) {
            unsigned typ = get16(cdef + 2 + 6 * c + 2);
            has[9] |= typ == 1;
            has[10] |= typ == 2;
        }
        has[4] |= s[j].rsiz == 2;
        has[5] |= s[j].rsiz != 1 && s[j].rsiz != 2;
    }
    has[2] = n > 1;
    has[15] = links != 0;
    for (i = 0; i < 16; i++)
        if (has[i])
            features[k++] = i;
    ml = (k + 7) / 8;
    box[len++] = (uint8_t)ml;
    for (i = 0; i < 2; i++) {                    /* FUAM, DCM: every feature */
        unsigned long long all = ((1ULL << k) - 1) << (8 * ml - k);
        int b;
        for (b = ml - 1; b >= 0; b--)
            box[len++] = (uint8_t)(all >> (8 * b));
    }
    box[len++] = 0;
    box[len++] = (uint8_t)k;                     /* NSF */
    for (i = 0; i < k; i++) {
        unsigned long long mask = 1ULL << (8 * ml - i - 1);
        int b;
        box[len++] = 0;
        box[len++] = (uint8_t)features[i];
        for (b = ml - 1; b >= 0; b--)
            box[len++] = (uint8_t)(mask >> (8 * b));
    }
    box[len++] = 0;
    box[len++] = 0;                              /* NVF */
    put32(box, (uint32_t)len);
    put32(box + 4, BOX_RREQ);
    return hv_write_bytes(out, box, len);
}

/* The URL of a linked file: file:// and its absolute path, resolved, with
 * the bytes Python's urllib.parse.quote_from_bytes escapes (as pathlib's
 * as_uri, which hvJP2K uses). NULL, with a message in error. */
static char *link_url(const char *path, char *error, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    char *real = realpath(path, NULL), *url, *p;
    const char *q;
    if (real == NULL) {
        fail(error, size, "%s: cannot resolve the path", path);
        return NULL;
    }
    if ((url = malloc(7 + 3 * strlen(real) + 1)) == NULL) {
        free(real);
        fail(error, size, "out of memory");
        return NULL;
    }
    memcpy(url, "file://", 7);
    for (p = url + 7, q = real; *q; q++) {
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

int hv_merge_files(const hv_merge_input *inputs, size_t n, int links, FILE *file, char *error,
                 size_t error_size) {
    static const uint8_t signature[12] = {0, 0, 0, 12, 0x6A, 0x50, 0x20, 0x20,
                                          0x0D, 0x0A, 0x87, 0x0A};
    static const uint8_t ftyp_embedded[] = {0, 0, 0, 28, 'f', 't', 'y', 'p', 'j', 'p', 'x', ' ',
                                            0, 0, 0, 1, 'j', 'p', 'x', ' ', 'j', 'p', '2', ' ',
                                            'j', 'p', 'x', 'b'};
    static const uint8_t ftyp_linked[] = {0, 0, 0, 20, 'f', 't', 'y', 'p', 'j', 'p', 'x', ' ',
                                          0, 0, 0, 1, 'j', 'p', 'x', ' '};
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
        return fail(error, error_size, "no JP2 input files");
    if (links && n > 65535)
        return fail(error, error_size, "a linked JPX file holds at most 65535 links");
    if (n > 0xFFFFFF)
        return fail(error, error_size, "more than 16777215 input files");
    if ((s = calloc(n, sizeof *s)) == NULL || (links && (urls = calloc(n, sizeof *urls)) == NULL)) {
        free(s);
        return fail(error, error_size, "out of memory");
    }
    hv_out_init(&w.out);
    for (i = 0; i < n; i++)
        if (read_source(&inputs[i], &s[i], error, error_size) != 0)
            goto done;
    for (i = 0; links && i < n; i++) {
        const char *rule;
        if ((urls[i] = link_url(inputs[i].path, error, error_size)) == NULL)
            goto done;
        if ((rule = hv_rule_url(0, 0, (const uint8_t *)urls[i], strlen(urls[i]) + 1)) != NULL) {
            fail(error, error_size, "%s: %s", inputs[i].path, rule);
            goto done;
        }
    }

    if (hv_write_bytes(&w.out, signature, sizeof signature) != 0 ||
        (links ? hv_write_bytes(&w.out, ftyp_linked, sizeof ftyp_linked)
               : hv_write_bytes(&w.out, ftyp_embedded, sizeof ftyp_embedded)) != 0 ||
        write_rreq(&w.out, s, n, links) != 0 || write_jp2h(&w.out, &s[0]) != 0 || flush(&w) != 0)
        goto done;
    for (i = 0; i < n; i++) {
        const hv_box *cs = &s[i].jp2c, *xml = &s[i].xml;
        if (write_headers(&w.out, &s[i], &s[0]) != 0)
            goto done;
        if (links) {
            if (hv_write_box_header(&w.out, HV_BOX_FTBL, 24) != 0 ||
                hv_write_flst(&w.out, cs->payload, (uint32_t)(cs->end - cs->payload),
                              (uint16_t)(i + 1)) != 0)
                goto done;
        } else if (hv_write_box_header(&w.out, HV_BOX_JP2C, cs->end - cs->payload) != 0 ||
                   write_through(&w, inputs[i].buf + cs->payload, cs->end - cs->payload) != 0) {
            goto done;
        }
        if (xml->type != 0) {
            /* The XML box as read, associated with codestream i: nlst
             * holds codestream i (0x01000000 + i) and compositing layer i
             * (0x02000000 + i). An XML box running to the end of its file
             * gets an explicit length. */
            size_t payload = xml->end - xml->payload, box = xml->to_end ? 8 + payload
                                                                           : xml->end - xml->start;
            uint8_t nlst[16] = {0, 0, 0, 16, 'n', 'l', 's', 't'};
            put32(nlst + 8, 0x01000000u + (uint32_t)i);
            put32(nlst + 12, 0x02000000u + (uint32_t)i);
            if (hv_write_box_header(&w.out, BOX_ASOC, 16 + (uint64_t)box) != 0 ||
                hv_write_bytes(&w.out, nlst, sizeof nlst) != 0 ||
                (xml->to_end ? copy_box(&w.out, &s[i], xml) != 0
                             : write_through(&w, inputs[i].buf + xml->start, box) != 0))
                goto done;
        }
        if (flush(&w) != 0)
            goto done;
    }
    if (links) {
        if (hv_begin_box(&w.out, HV_BOX_DTBL, 0, &start) != 0 || hv_write_ndr(&w.out, (uint16_t)n) != 0)
            goto done;
        for (i = 0; i < n; i++)
            if (hv_write_url(&w.out, urls[i]) != 0)
                goto done;
        if (hv_end_box(&w.out, start) != 0)
            goto done;
    }
    if (flush(&w) != 0 || fflush(file) != 0) {
        if (error[0] == 0)
            fail(error, error_size, "cannot write the JPX file");
        goto done;
    }
    status = 0;

done:
    if (status != 0 && error[0] == 0)
        fail(error, error_size, "%s", w.out.error ? w.out.error : "out of memory");
    for (i = 0; urls != NULL && i < n; i++)
        free(urls[i]);
    free(urls);
    free(s);
    hv_out_free(&w.out);
    return status;
}
