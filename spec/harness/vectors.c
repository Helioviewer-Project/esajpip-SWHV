/* vectors.c — builds the JPEG 2000 test-vector corpus from the ACN model.
 *
 * Usage:  vectors <output-directory>
 *
 * Produces complete .jp2 / .jpx files plus manifest.tsv. Every vector is
 * labelled at both layers by decoding it with the generated decoders, running
 * the generated constraint checkers, and applying crossfield.c. Labels are
 * evaluated mechanically; the model, rules and mutant expectations are
 * human-authored and checked against the cited standards.
 *
 * Structure:
 *   1. Generated-API adaptation (the only place that names generated symbols
 *      beyond field access).
 *   2. Byte-level walker: finds every Lxxx / Psot / LBox in an encoded file
 *      and the codestream extent inside a jp2c. Independent of the model.
 *   3. Base builders: canonical profile-valid JP2, embedded JPX, linked JPX.
 *   4. Labelling and emission.
 *   5. Mutation catalogues: field, rule, length, region.
 *
 * Build (see spec/README.md):
 *   GEN=$(ls /tmp/j2k-gen/[a-z]*.c | grep -v -e mainprogram -e test_case -e auto_tcs)
 *   cc -O1 -g -fsanitize=address,undefined -I/tmp/j2k-gen \
 *      vectors.c crossfield.c mapping.c $GEN -o vectors
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asn1crt.h"
#include "asn1crt_encoding.h"
#include "asn1crt_encoding_acn.h"
#include "crossfield.h"

/* ------------------------------------------------------------------------ */
/* 1. Generated-API adaptation                                              */
/* ------------------------------------------------------------------------ */

/* asn1scc 4.x C conventions. If a generated name differs, change it here. */
#define ENC(T)   T##_ACN_Encode
#define DEC(T)   T##_ACN_Decode
#define VALID(T) T##_IsConstraintValid
#define INIT(T)  T##_Initialize

/* Encoded size upper bound for the top-level file types. asn1scc emits
 * <Type>_REQUIRED_BYTES_FOR_ACN_ENCODING; it is an upper bound over the
 * corpus SIZE constraints, which is exactly what we want for the buffer. */
#ifndef FILE_BUFFER_BYTES
#define FILE_BUFFER_BYTES Jp2Family_REQUIRED_BYTES_FOR_ACN_ENCODING
#endif


/* ------------------------------------------------------------------------ */
/* Small utilities                                                          */
/* ------------------------------------------------------------------------ */

typedef struct {
    unsigned char *data;
    size_t len;
} Bytes;

static void die(const char *what) {
    fprintf(stderr, "vectors: %s\n", what);
    exit(EXIT_FAILURE);
}

static void *xcalloc(size_t n) {
    void *p = calloc(1, n);
    if (!p) die("out of memory");
    return p;
}

static uint32_t be32(const unsigned char *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static uint16_t be16(const unsigned char *p) { return (uint16_t) ((p[0] << 8) | p[1]); }
static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char) (v >> 24); p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);  p[3] = (unsigned char) v;
}
static void put16(unsigned char *p, uint16_t v) { p[0] = (unsigned char) (v >> 8); p[1] = (unsigned char) v; }

static Bytes bytes_dup(Bytes b) {
    Bytes r = { xcalloc(b.len + 1), b.len };
    memcpy(r.data, b.data, b.len);
    return r;
}

static void write_file(const char *dir, const char *name, Bytes b) {
    char path[1024];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f || fwrite(b.data, 1, b.len, f) != b.len || fclose(f) != 0) {
        fprintf(stderr, "vectors: cannot write %s: %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/* ------------------------------------------------------------------------ */
/* 2. Byte-level walker                                                     */
/* ------------------------------------------------------------------------ */

/* A length-prefixed region in an encoded file. `len_off` is the offset of
 * the length field, `len_width` its width in bytes; `start`/`end` bound the
 * whole region (marker code / LBox included) and `payload` its content. */
typedef struct {
    const char *kind;      /* "LBox", "Lxxx", "Psot" */
    size_t start, end;
    size_t len_off;
    int len_width;
    size_t payload_start;
    int parent;            /* index into the region list, or -1 */
} Region;

typedef struct {
    Region *r;
    int n, cap;
    size_t soc_off, eoc_end;   /* first codestream found (for companions) */
} Regions;

static int region_add(Regions *rs, Region reg) {
    if (rs->n == rs->cap) {
        rs->cap = rs->cap ? rs->cap * 2 : 64;
        rs->r = realloc(rs->r, (size_t) rs->cap * sizeof *rs->r);
        if (!rs->r) die("out of memory");
    }
    rs->r[rs->n] = reg;
    return rs->n++;
}

static int is_superbox(uint32_t t) {
    return t == HV_BOX_JP2H || t == HV_BOX_JPCH || t == HV_BOX_FTBL || t == HV_BOX_DTBL ||
           t == HV_BOX_ASOC || t == HV_BOX_JPLH || t == HV_BOX_UINF;
}

static int walk_codestream(Regions *rs, const Bytes *b, size_t pos, size_t end, int parent) {
    size_t soc = pos;
    if (end - pos < 2 || be16(b->data + pos) != HV_SOC) return 0;
    pos += 2;
    while (pos + 2 <= end) {
        uint16_t code = be16(b->data + pos);
        if (code == HV_EOC) {
            if (rs->soc_off == 0 && rs->eoc_end == 0) { rs->soc_off = soc; rs->eoc_end = pos + 2; }
            return 1;
        }
        if (code == HV_SOT) {
            uint32_t psot;
            size_t tp_end, p;
            int tp;
            if (pos + 12 > end) return 0;
            psot = be32(b->data + pos + 6);
            tp_end = pos + psot;
            if (psot < 14 || tp_end > end) return 0;
            tp = region_add(rs, (Region) { "Psot", pos, tp_end, pos + 6, 4, pos + 12, parent });
            p = pos + 12;
            while (p + 2 <= tp_end) {
                uint16_t c = be16(b->data + p);
                uint16_t l;
                if (c == HV_SOD) break;
                if (p + 4 > tp_end) return 0;
                l = be16(b->data + p + 2);
                if (l < 2 || p + 2 + l > tp_end) return 0;
                region_add(rs, (Region) { "Lxxx", p, p + 2 + l, p + 2, 2, p + 4, tp });
                p += 2 + l;
            }
            pos = tp_end;
            continue;
        }
        {
            uint16_t l;
            if (pos + 4 > end) return 0;
            l = be16(b->data + pos + 2);
            if (l < 2 || pos + 2 + l > end) return 0;
            region_add(rs, (Region) { "Lxxx", pos, pos + 2 + l, pos + 2, 2, pos + 4, parent });
            pos += 2 + l;
        }
    }
    return 0;
}

static int walk_boxes(Regions *rs, const Bytes *b, size_t pos, size_t end, int parent) {
    while (pos < end) {
        uint32_t l, t;
        size_t box_end;
        int idx;
        if (end - pos < 8) return 0;
        l = be32(b->data + pos);
        t = be32(b->data + pos + 4);
        if (l < 8) return 0;                     /* XLBox and L = 0 are not in the corpus */
        box_end = pos + l;
        if (box_end > end) return 0;
        idx = region_add(rs, (Region) { "LBox", pos, box_end, pos, 4, pos + 8, parent });
        if (t == HV_BOX_JP2C) {
            if (!walk_codestream(rs, b, pos + 8, box_end, idx)) return 0;
        } else if (is_superbox(t)) {
            size_t child_start = pos + 8 + (t == HV_BOX_DTBL ? 2 : 0);
            if (!walk_boxes(rs, b, child_start, box_end, idx)) return 0;
        }
        pos = box_end;
    }
    return 1;
}

static Regions walk(const Bytes *b) {
    Regions rs = { NULL, 0, 0, 0, 0 };
    if (!walk_boxes(&rs, b, 0, b->len, -1)) die("walker: generated file is not structurally sound");
    return rs;
}

/* ------------------------------------------------------------------------ */
/* 3. Base builders (layer-1 structs, profile-valid)                        */
/* ------------------------------------------------------------------------ */

#define OCTETS(field, src, n) \
    do { memcpy((field).arr, (src), (n)); (field).nCount = (int) (n); } while (0)
#define FIXED_OCTETS(field, src, n) \
    do { memcpy((field).arr, (src), (n)); } while (0)

static void build_siz(Siz *s, int width, int height) {
    s->rsiz = 0;
    s->xsiz = width;  s->ysiz = height;
    s->xosiz = 0;     s->yosiz = 0;
    s->xtsiz = width; s->ytsiz = height;
    s->xtosiz = 0;    s->ytosiz = 0;
    s->csiz = 1;
    s->components.nCount = 1;
    s->components.arr[0].isSigned = 0;
    s->components.arr[0].depthMinus1 = 7;
    s->components.arr[0].xrsiz = 1;
    s->components.arr[0].yrsiz = 1;
}

static void build_cod(Cod *c, int levels, int custom_precincts) {
    int i;
    c->scod.reserved = 0;
    c->scod.ephMarkers = 0;
    c->scod.sopMarkers = 0;
    c->scod.customPrecincts = custom_precincts;
    c->sgcod.progression = 0;                  /* LRCP */
    c->sgcod.layers = 1;
    c->sgcod.mct = 0;
    c->spcod.levels = levels;
    c->spcod.cbWidthExp = 4;
    c->spcod.cbHeightExp = 4;
    c->spcod.cbStyle = 0;
    c->spcod.transform = 1;                    /* 5-3 reversible */
    c->spcod.precincts.nCount = custom_precincts ? levels + 1 : 0;
    if (custom_precincts) {
        for (i = 0; i <= levels; ++i) {
            c->spcod.precincts.arr[i].ppx = 15;
            c->spcod.precincts.arr[i].ppy = 15;
        }
    }
}

/* One packet per (layer, resolution, component, precinct); with the defaults
 * above that is levels + 1 packets, each an empty packet: header bit 0. */
static void build_tile_part(TilePart *tp, int levels, int with_plt) {
    int packets = levels + 1, i;
    tp->lsot = 10;
    tp->isot = 0;
    tp->tpsot = 0;
    tp->tnsot = 1;
    tp->rest.headers.nCount = with_plt ? 1 : 0;
    if (with_plt) {
        TileSegment *ts = &tp->rest.headers.arr[0];
        Plt *plt;
        ts->code = HV_PLT;
        ts->exist.plt = 1;
        plt = &ts->plt.body;
        plt->zplt = 0;
        plt->entries.nCount = packets;
        for (i = 0; i < packets; ++i) {
            Iplt *e = &plt->entries.arr[i];
            memset(e, 0, sizeof *e);
            e->b0.bits = 1;                    /* packet length 1 */
        }
    }
    tp->rest.data.nCount = packets;
    for (i = 0; i < packets; ++i) tp->rest.data.arr[i] = 0x00;
}

static void build_codestream(Codestream *cs, int width, int height, int levels,
                             int custom_precincts, int with_plt) {
    MainSegment *seg;
    cs->soc = HV_SOC;
    cs->sizCode = HV_SIZ;
    build_siz(&cs->siz.body, width, height);
    cs->segments.nCount = 3;

    seg = &cs->segments.arr[0];
    seg->code = HV_COD;
    seg->exist.cod = 1;
    build_cod(&seg->cod.body, levels, custom_precincts);

    seg = &cs->segments.arr[1];
    seg->code = HV_QCD;
    seg->exist.qcd = 1;
    seg->qcd.body.sqcd = 0x40;          /* 2 guard bits, no quantization */
    {
        /* one SPqcd byte per sub-band: 3 * levels + 1 */
        int n = 3 * levels + 1, i;
        seg->qcd.body.spqcd.nCount = n;
        for (i = 0; i < n; ++i) seg->qcd.body.spqcd.arr[i] = 0x40;
    }

    seg = &cs->segments.arr[2];
    seg->code = HV_SOT;
    seg->exist.tilePart = 1;
    build_tile_part(&seg->tilePart, levels, with_plt);
}

static void add_opaque_box(Jp2Family *f, uint32_t type, const void *data, size_t n) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    switch (type) {
        case HV_BOX_JP:   b->payload.kind = TopPayload_jP_PRESENT;   OCTETS(b->payload.u.jP.data, data, n); break;
        default: die("add_opaque_box: unsupported type");
    }
}

static void add_signature_and_ftyp(Jp2Family *f, const char *brand) {
    TopBox *b;
    Ftyp *ftyp;
    add_opaque_box(f, HV_BOX_JP, "\x0D\x0A\x87\x0A", 4);
    b = &f->boxes.arr[f->boxes.nCount++];
    b->payload.kind = TopPayload_ftyp_PRESENT;
    ftyp = &b->payload.u.ftyp;
    FIXED_OCTETS(ftyp->brand, brand, 4);
    /* MinV: T.800 I.5.2 requires 0 in a JP2 file, T.801 M.8 requires 1 in a
     * JPX file. Readers shall parse the file whatever the value, and esajpip
     * ignores it, so neither layer constrains it; the base vectors carry the
     * conforming value so that a valid vector is valid to a strict reader. */
    ftyp->minor = memcmp(brand, "jpx ", 4) == 0 ? 1 : 0;
    ftyp->compat.nCount = 1;
    FIXED_OCTETS(ftyp->compat.arr[0], brand, 4);
}

/* A mask of `ml` bytes: `bits` as a big-endian 32-bit value, then zeros
 * (bit 0 of a mask is the most significant bit of its first byte). */
static void set_mask(RreqMask *m, int ml, uint32_t bits) {
    int i;
    m->nCount = ml;
    for (i = 0; i < ml; ++i) m->arr[i] = (unsigned char) (i < 4 ? bits >> (24 - 8 * i) : 0);
}

/* Reader Requirements as hvJP2K writes them (T.801 M.11.1): one standard
 * feature per mask bit, every one needed both to understand the file fully
 * and to display it: 1 (no extensions), 2 (multiple compositing layers), 5
 * (unrestricted Part 1 codestream), and 15 (fragments in locally accessible
 * files) when linked. */
static void add_rreq(Jp2Family *f, int linked) {
    static const int features[] = { 1, 2, 5, 15 };
    int n = linked ? 4 : 3, i;
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    Rreq *r;
    uint32_t all = 0;
    b->payload.kind = TopPayload_rreq_PRESENT;
    r = &b->payload.u.rreq;
    r->standard.nCount = n;
    for (i = 0; i < n; ++i) {
        r->standard.arr[i].sf = features[i];
        set_mask(&r->standard.arr[i].sm, 1, 0x80000000u >> i);
        all |= 0x80000000u >> i;
    }
    set_mask(&r->fuam, 1, all);
    set_mask(&r->dcm, 1, all);
    r->vendor.nCount = 0;
}

/* Canonical files. Sizes 4x4 leave room for origin/tile mutants. */
#define BASE_W 4
#define BASE_H 4

/* Header boxes (T.800 I.5.3) for the base codestream: one unsigned 8-bit
 * component. */
static void set_ihdr(InnerBox *c, int width, int height) {
    Ihdr *ihdr = &c->payload.u.ihdr;
    c->payload.kind = InnerPayload_ihdr_PRESENT;
    ihdr->height = height;
    ihdr->width = width;
    ihdr->nc = 1;
    ihdr->bpc = 7;
    ihdr->c = 7;
    ihdr->unkc = 0;
    ihdr->ipr = 0;
}

static void set_colr(InnerBox *c, int meth, uint32_t enumcs) {
    ColrHeader *h = &c->payload.u.colr.header;
    c->payload.kind = InnerPayload_colr_PRESENT;
    h->meth = meth;
    h->prec = 0;
    h->approx = 0;
    h->exist.enumcs = meth == 1;
    h->enumcs = enumcs;
    c->payload.u.colr.rest.nCount = 0;
}

static void add_jp2h(Jp2Family *f, int width, int height) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->payload.kind = TopPayload_jp2h_PRESENT;
    b->payload.u.jp2h.children.nCount = 2;
    set_ihdr(&b->payload.u.jp2h.children.arr[0], width, height);
    set_colr(&b->payload.u.jp2h.children.arr[1], 1, 17);          /* greyscale */
}

static Codestream *add_jp2c(Jp2Family *f, int width, int height, int levels,
                            int custom_precincts, int with_plt) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->payload.kind = TopPayload_jp2c_PRESENT;
    build_codestream(&b->payload.u.jp2c, width, height, levels, custom_precincts, with_plt);
    return &b->payload.u.jp2c;
}

/* A Codestream Header box with the image header of a base codestream, as
 * hvJP2K writes one where the codestream's header differs from jp2h's. */
static void add_jpch(Jp2Family *f) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->payload.kind = TopPayload_jpch_PRESENT;
    b->payload.u.jpch.children.nCount = 1;
    set_ihdr(&b->payload.u.jpch.children.arr[0], BASE_W, BASE_H);
}

static void add_ftbl(Jp2Family *f, uint64_t off, uint32_t len, int dr) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    InnerBox *c;
    b->payload.kind = TopPayload_ftbl_PRESENT;
    b->payload.u.ftbl.children.nCount = 1;
    c = &b->payload.u.ftbl.children.arr[0];
    c->payload.kind = InnerPayload_flst_PRESENT;
    c->payload.u.flst.nf = 1;
    c->payload.u.flst.fragments.nCount = 1;
    c->payload.u.flst.fragments.arr[0].off = off;
    c->payload.u.flst.fragments.arr[0].len = len;
    c->payload.u.flst.fragments.arr[0].dr = dr;
}

static void add_dtbl(Jp2Family *f, const char *const *urls, int n) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    int i;
    b->payload.kind = TopPayload_dtbl_PRESENT;
    b->payload.u.dtbl.ndr = n;
    b->payload.u.dtbl.references.nCount = n;
    for (i = 0; i < n; ++i) {
        InnerBox *c = &b->payload.u.dtbl.references.arr[i];
        c->payload.kind = InnerPayload_url_PRESENT;
        c->payload.u.url.vers = 0;
        c->payload.u.url.flag = 0;
        strncpy((char *) c->payload.u.url.loc, urls[i], sizeof c->payload.u.url.loc - 1);
    }
}

typedef struct {
    const char *name;
    cf_kind kind;
    Jp2Family *file;
    /* For linked JPX: companion codestream files and the codestream index
     * whose fields the field-mutant table addresses (-1 when none). */
    int jp2c_box;
} Base;

static Jp2Family *new_family(void) {
    Jp2Family *f = xcalloc(sizeof *f);
    return f;
}

static Base base_jp2(int levels, int custom_precincts) {
    Base b = { custom_precincts ? "jp2-precincts" : "jp2", CF_JP2, new_family(), 3 };
    add_signature_and_ftyp(b.file, "jp2 ");
    add_jp2h(b.file, BASE_W, BASE_H);
    add_jp2c(b.file, BASE_W, BASE_H, levels, custom_precincts, 1);
    return b;
}

static Base base_jpx_embedded(void) {
    Base b = { "jpx-embedded", CF_JPX, new_family(), 6 };
    add_signature_and_ftyp(b.file, "jpx ");
    add_rreq(b.file, 0);
    add_jp2h(b.file, BASE_W, BASE_H);
    add_jpch(b.file);
    add_jpch(b.file);
    add_jp2c(b.file, BASE_W, BASE_H, 0, 0, 1);
    add_jp2c(b.file, BASE_W, BASE_H, 0, 0, 1);
    return b;
}

/* Linked JPX needs the referenced files' codestream extents; caller
 * supplies them after encoding the companions. */
static Base base_jpx_linked(const char *const *urls, const uint64_t *off, const uint32_t *len, int n) {
    Base b = { "jpx-linked", CF_JPX, new_family(), -1 };
    int i;
    add_signature_and_ftyp(b.file, "jpx ");
    add_rreq(b.file, 1);
    add_jp2h(b.file, BASE_W, BASE_H);
    for (i = 0; i < n; ++i) add_jpch(b.file);
    for (i = 0; i < n; ++i) add_ftbl(b.file, off[i], len[i], i + 1);
    add_dtbl(b.file, urls, n);
    return b;
}

/* ------------------------------------------------------------------------ */
/* 4. Encoding, labelling, emission                                          */
/* ------------------------------------------------------------------------ */

static unsigned char *encode_buffer;   /* FILE_BUFFER_BYTES, allocated once */

static Bytes encode_family(const Jp2Family *f, int check_constraints) {
    BitStream bs;
    int err = 0;
    Bytes out;
    BitStream_Init(&bs, encode_buffer, FILE_BUFFER_BYTES);
    if (!ENC(Jp2Family)(f, &bs, &err, check_constraints)) {
        fprintf(stderr, "vectors: encode failed, error %d (constraint checking %s)\n",
                err, check_constraints ? "on" : "off");
        exit(EXIT_FAILURE);
    }
    out.len = (size_t) BitStream_GetLength(&bs);
    out.data = xcalloc(out.len + 1);
    memcpy(out.data, encode_buffer, out.len);
    return out;
}

typedef struct {
    int std_ok, prof_ok;
    const char *std_reason, *prof_reason;
} Label;

/* What a mutant is meant to produce. The decoders decide the label; the
 * expectation only checks that the mutant did what its note says, so a
 * setter that misses its target or a rule that stopped firing is caught at
 * generation time instead of surfacing as a puzzling server-test result. */
typedef enum {
    X_VALID,   /* valid at both layers */
    X_STD,     /* invalid at layer 1; layer 2 may accept an intentional leniency */
    X_PROF     /* valid at layer 1, invalid at layer 2 */
} Expect;

static Jp2Family *dec_family;            /* reused decode targets */
static Jp2File_Profile *dec_jp2;
static JpxFile_Profile *dec_jpx;

/* Extents measured from the encoded companion files, not from JPX claims.
 * This is a corpus oracle, not a general filesystem resolver. */
static struct {
    const char *url;
    uint64_t offset;
    uint32_t length;
} companions[3];
static int companion_count;

static const char *check_linked_extents(const JpxFile_Profile *file) {
    const DataReferences_Profile *references = NULL;
    int i, j, k;
    for (i = 0; i < file->boxes.nCount; ++i)
        if (file->boxes.arr[i].payload.kind == TopPayload_Profile_dtbl_PRESENT)
            references = &file->boxes.arr[i].payload.u.dtbl;
    if (references == NULL) return NULL; /* embedded JPX */
    for (i = 0; i < file->boxes.nCount; ++i) {
        const TopBox_Profile *box = &file->boxes.arr[i];
        if (box->payload.kind != TopPayload_Profile_ftbl_PRESENT) continue;
        for (k = 0; k < box->payload.u.ftbl.children.nCount; ++k) {
            const InnerBox_Profile *child = &box->payload.u.ftbl.children.arr[k];
            const Fragment *fragment;
            const char *url;
            if (child->payload.kind != InnerPayload_Profile_flst_PRESENT) continue;
            /* Structural profile checks already required one external
             * fragment with an in-range DR. */
            fragment = &child->payload.u.flst.fragments.arr[0];
            url = (const char *) references->references.arr[fragment->dr - 1].payload.u.url.loc;
            for (j = 0; j < companion_count; ++j)
                if (strcmp(url, companions[j].url) == 0) break;
            if (j == companion_count) return "url.missing-companion";
            if (fragment->off != companions[j].offset || fragment->len != companions[j].length)
                return "flst.source-extent";
        }
    }
    return NULL;
}

/* The generated CONTAINING decoder temporarily trusts LBox as its stream
 * size. Check physical box boundaries first so a malformed LBox cannot make
 * an opaque unknown box read beyond the input buffer. Profile metadata boxes
 * are opaque where their internal framing is not interpreted by the server:
 * profile 1 is a .jpx at layer 2, profile 2 a .jp2, where every box but
 * jp2c is opaque. */
static int box_bounds_ok(const Bytes *b, size_t start, size_t end, int profile, int top) {
    while (start < end) {
        uint32_t length, type;
        size_t box_end, child_start;
        int children;
        if (end - start < 8) return 0;
        length = be32(b->data + start);
        type = be32(b->data + start + 4);
        if (length < 8 || length > end - start) return 0;
        box_end = start + length;
        children = top && (profile == 2 ? 0
                           : profile ? (type == HV_BOX_JPCH || type == HV_BOX_FTBL || type == HV_BOX_DTBL)
                           : is_superbox(type));
        if (children) {
            child_start = start + 8 + (type == HV_BOX_DTBL ? 2 : 0);
            if (child_start > box_end || !box_bounds_ok(b, child_start, box_end, profile, 0))
                return 0;
        }
        start = box_end;
    }
    return 1;
}

static Label label(Bytes b, cf_kind kind) {
    Label l = { 0, 0, NULL, NULL };
    BitStream bs;
    int err = 0;
    const char *r;

    BitStream_AttachBuffer(&bs, b.data, (long) b.len);
    INIT(Jp2Family)(dec_family);
    if (!box_bounds_ok(&b, 0, b.len, 0, 1) ||
        !DEC(Jp2Family)(dec_family, &bs, &err)) l.std_reason = "decode";
    else if (!VALID(Jp2Family)(dec_family, &err)) l.std_reason = "constraint";
    else if ((r = cf_check_family(dec_family, CF_STANDARD, kind)) != NULL) l.std_reason = r;
    else l.std_ok = 1;

    BitStream_AttachBuffer(&bs, b.data, (long) b.len);
    if (kind == CF_JP2) {
        INIT(Jp2File_Profile)(dec_jp2);
        if (!box_bounds_ok(&b, 0, b.len, 2, 1) ||
            !DEC(Jp2File_Profile)(dec_jp2, &bs, &err)) l.prof_reason = "decode";
        else if (!VALID(Jp2File_Profile)(dec_jp2, &err)) l.prof_reason = "constraint";
        else if ((r = cf_check_jp2_profile(dec_jp2)) != NULL) l.prof_reason = r;
        else l.prof_ok = 1;
    } else {
        INIT(JpxFile_Profile)(dec_jpx);
        if (!box_bounds_ok(&b, 0, b.len, 1, 1) ||
            !DEC(JpxFile_Profile)(dec_jpx, &bs, &err)) l.prof_reason = "decode";
        else if (!VALID(JpxFile_Profile)(dec_jpx, &err)) l.prof_reason = "constraint";
        else if ((r = cf_check_jpx_profile(dec_jpx)) != NULL) l.prof_reason = r;
        else if ((r = check_linked_extents(dec_jpx)) != NULL) l.prof_reason = r;
        else l.prof_ok = 1;
    }
    return l;
}

static FILE *manifest;
static const char *out_dir;
static int emitted, emitted_valid, mismatches;

static const char *expect_name(Expect x) {
    return x == X_VALID ? "valid/valid" : x == X_STD ? "invalid/-" : "valid/invalid";
}

/* Emit one vector: write the file, label it, append a manifest row, and
 * check the label against the mutant's expectation. */
static void emit(Bytes b, cf_kind kind, const char *name, const char *field,
                 const char *note, const char *companions, Expect expect) {
    char file[256];
    Label l = label(b, kind);
    const char *reason = !l.std_ok ? l.std_reason : !l.prof_ok ? l.prof_reason : "-";
    int as_expected = expect == X_VALID ? (l.std_ok && l.prof_ok)
                    : expect == X_STD   ? !l.std_ok
                    :                     (l.std_ok && !l.prof_ok);
    snprintf(file, sizeof file, "%s.%s", name, kind == CF_JP2 ? "jp2" : "jpx");
    write_file(out_dir, file, b);
    fprintf(manifest, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", file,
            kind == CF_JP2 ? "jp2" : "jpx",
            l.std_ok ? "valid" : "invalid",
            l.prof_ok ? "valid" : "invalid",
            reason,
            field ? field : "-",
            note ? note : "-",
            companions ? companions : "-");
    emitted++;
    if (l.std_ok && l.prof_ok) emitted_valid++;
    if (!as_expected) {
        mismatches++;
        fprintf(stderr, "vectors: %s: expected %s, labelled %s/%s (%s)\n", file,
                expect_name(expect), l.std_ok ? "valid" : "invalid",
                l.prof_ok ? "valid" : "invalid", reason);
    }
}

/* ------------------------------------------------------------------------ */
/* 5a. Field mutants                                                         */
/* ------------------------------------------------------------------------ */

/* Each entry sets one scalar field of the base's codestream (or box tree)
 * to one interesting value. Values must be representable at the field's
 * ACN width (the encoder runs with constraint checking off but cannot
 * exceed the width). Labels come from the decoders, never from here. */
typedef void (*Setter)(Jp2Family *f, int box, asn1SccSint v);

static Codestream *cs_of(Jp2Family *f, int box) { return &f->boxes.arr[box].payload.u.jp2c; }
static Cod *cod_of(Jp2Family *f, int box) { return &cs_of(f, box)->segments.arr[0].cod.body; }
static TilePart *tp_of(Jp2Family *f, int box) { return &cs_of(f, box)->segments.arr[2].tilePart; }

#define SETTER(id, lvalue) \
    static void set_##id(Jp2Family *f, int box, asn1SccSint v) { (void) f; (void) box; lvalue = v; }

SETTER(siz_rsiz,   cs_of(f, box)->siz.body.rsiz)
SETTER(siz_xsiz,   cs_of(f, box)->siz.body.xsiz)
SETTER(siz_ysiz,   cs_of(f, box)->siz.body.ysiz)
SETTER(siz_xosiz,  cs_of(f, box)->siz.body.xosiz)
SETTER(siz_yosiz,  cs_of(f, box)->siz.body.yosiz)
SETTER(siz_xtsiz,  cs_of(f, box)->siz.body.xtsiz)
SETTER(siz_ytsiz,  cs_of(f, box)->siz.body.ytsiz)
SETTER(siz_xtosiz, cs_of(f, box)->siz.body.xtosiz)
SETTER(siz_ytosiz, cs_of(f, box)->siz.body.ytosiz)
SETTER(siz_csiz,   cs_of(f, box)->siz.body.csiz)
SETTER(siz_depth,  cs_of(f, box)->siz.body.components.arr[0].depthMinus1)
SETTER(siz_xrsiz,  cs_of(f, box)->siz.body.components.arr[0].xrsiz)
SETTER(siz_yrsiz,  cs_of(f, box)->siz.body.components.arr[0].yrsiz)
SETTER(cod_reserved,    cod_of(f, box)->scod.reserved)
SETTER(cod_sop,         cod_of(f, box)->scod.sopMarkers)
SETTER(cod_progression, cod_of(f, box)->sgcod.progression)
SETTER(cod_layers,      cod_of(f, box)->sgcod.layers)
SETTER(cod_mct,         cod_of(f, box)->sgcod.mct)
SETTER(cod_levels,      cod_of(f, box)->spcod.levels)
SETTER(cod_cbw,         cod_of(f, box)->spcod.cbWidthExp)
SETTER(cod_cbh,         cod_of(f, box)->spcod.cbHeightExp)
SETTER(cod_cbstyle,     cod_of(f, box)->spcod.cbStyle)
SETTER(cod_transform,   cod_of(f, box)->spcod.transform)
SETTER(cod_ppx_higher,  cod_of(f, box)->spcod.precincts.arr[1].ppx)
SETTER(cod_ppy_higher,  cod_of(f, box)->spcod.precincts.arr[1].ppy)
SETTER(sot_lsot,   tp_of(f, box)->lsot)
SETTER(sot_isot,   tp_of(f, box)->isot)
SETTER(sot_tpsot,  tp_of(f, box)->tpsot)
SETTER(sot_tnsot,  tp_of(f, box)->tnsot)
SETTER(plt_zplt,   tp_of(f, box)->rest.headers.arr[0].plt.body.zplt)
SETTER(iplt_bits,  tp_of(f, box)->rest.headers.arr[0].plt.body.entries.arr[0].b0.bits)

static void set_cod_ppx_lowest(Jp2Family *f, int box, asn1SccSint value) {
    TilePart *tp = tp_of(f, box);
    Iplt *entry;
    cod_of(f, box)->spcod.precincts.arr[0].ppx = value;
    if (value != 0)
        return;

    /* PPx=0 splits the 2x2 lowest-resolution image into two horizontal
     * precincts, so the complete codestream has three packets rather than
     * the base fixture's two. */
    entry = &tp->rest.headers.arr[0].plt.body.entries.arr[2];
    memset(entry, 0, sizeof *entry);
    entry->b0.bits = 1;
    tp->rest.headers.arr[0].plt.body.entries.nCount = 3;
    tp->rest.data.arr[2] = 0;
    tp->rest.data.nCount = 3;
}

typedef struct {
    const char *field;
    Setter set;
    asn1SccSint value;
    const char *note;
    int needs_precincts;
    Expect expect;
} FieldMutant;

static const FieldMutant field_mutants[] = {
    { "siz.rsiz", set_siz_rsiz, 2, "table A.10 value 2 (profile 1)", 0, X_VALID },
    { "siz.rsiz", set_siz_rsiz, 32768, "extension capability flag", 0, X_VALID },
    { "siz.xsiz", set_siz_xsiz, 0, "min-1", 0, X_STD },
    { "siz.xsiz", set_siz_xsiz, 2147483648, "profile max+1", 0, X_PROF },
    { "siz.xsiz", set_siz_xsiz, 4294967295u, "standard max (tile smaller: single-tile rule)", 0, X_PROF },
    { "siz.ysiz", set_siz_ysiz, 0, "min-1", 0, X_STD },
    { "siz.ysiz", set_siz_ysiz, 2147483648, "profile max+1", 0, X_PROF },
    { "siz.xosiz", set_siz_xosiz, 1, "profile max+1; standard valid", 0, X_PROF },
    { "siz.xosiz", set_siz_xosiz, 4, "== xsiz: empty image", 0, X_STD },
    { "siz.yosiz", set_siz_yosiz, 1, "profile max+1; standard valid", 0, X_PROF },
    { "siz.xtsiz", set_siz_xtsiz, 0, "min-1", 0, X_STD },
    { "siz.xtsiz", set_siz_xtsiz, 3, "below xsiz: two tiles", 0, X_PROF },
    { "siz.ytsiz", set_siz_ytsiz, 3, "below ysiz: two tiles", 0, X_PROF },
    { "siz.xtosiz", set_siz_xtosiz, 1, "tile origin past image origin", 0, X_STD },
    { "siz.ytosiz", set_siz_ytosiz, 1, "tile origin past image origin", 0, X_STD },
    { "siz.csiz", set_siz_csiz, 0, "min-1", 0, X_STD },
    { "siz.csiz", set_siz_csiz, 2, "count mismatch", 0, X_STD },
    { "siz.csiz", set_siz_csiz, 16385, "max+1 (and count mismatch)", 0, X_STD },
    { "siz.component.depthMinus1", set_siz_depth, 38, "max+1 (39-bit depth)", 0, X_STD },
    { "siz.component.xrsiz", set_siz_xrsiz, 0, "min-1", 0, X_STD },
    { "siz.component.yrsiz", set_siz_yrsiz, 0, "min-1", 0, X_STD },
    { "cod.scod.reserved", set_cod_reserved, 1, "reserved bit set", 0, X_STD },
    { "cod.scod.sopMarkers", set_cod_sop, 1, "SOP markers outside served packet representation", 0, X_PROF },
    { "cod.sgcod.progression", set_cod_progression, 5, "max+1", 0, X_STD },
    { "cod.sgcod.progression", set_cod_progression, 3, "PCRL with unit sampling: valid", 0, X_VALID },
    { "cod.sgcod.layers", set_cod_layers, 0, "min-1", 0, X_STD },
    { "cod.sgcod.mct", set_cod_mct, 2, "max+1", 0, X_STD },
    { "cod.sgcod.mct", set_cod_mct, 1, "MCT requires three components", 0, X_STD },
    { "cod.spcod.levels", set_cod_levels, 33, "max+1", 0, X_STD },
    { "cod.spcod.cbWidthExp", set_cod_cbw, 9, "max+1", 0, X_STD },
    { "cod.spcod.cbWidthExp", set_cod_cbw, 5, "5+4 = 9 > 8: code-block area", 0, X_STD },
    { "cod.spcod.cbHeightExp", set_cod_cbh, 9, "max+1", 0, X_STD },
    { "cod.spcod.cbStyle", set_cod_cbstyle, 63, "all Part 1 bits: valid", 0, X_VALID },
    { "cod.spcod.cbStyle", set_cod_cbstyle, 64, "reserved bit 6 (HTJ2K flag)", 0, X_STD },
    { "cod.spcod.cbStyle", set_cod_cbstyle, 255, "all bits", 0, X_STD },
    { "cod.spcod.transform", set_cod_transform, 2, "max+1", 0, X_STD },
    { "cod.spcod.precincts.lowest.ppx", set_cod_ppx_lowest, 0, "zero at r=0: valid", 1, X_VALID },
    { "cod.spcod.precincts.higher[0].ppx", set_cod_ppx_higher, 0, "zero above r=0 (Table A.21)", 1, X_STD },
    { "cod.spcod.precincts.higher[0].ppy", set_cod_ppy_higher, 0, "zero above r=0 (Table A.21)", 1, X_STD },
    { "sot.lsot", set_sot_lsot, 11, "Lsot != 10", 0, X_STD },
    { "sot.isot", set_sot_isot, 1, "tile 1 of a one-tile grid (A.4.2)", 0, X_STD },
    { "sot.isot", set_sot_isot, 65535, "standard max+1", 0, X_STD },
    { "sot.tpsot", set_sot_tpsot, 1, "first tile-part index 1: sequence", 0, X_STD },
    { "sot.tpsot", set_sot_tpsot, 255, "reserved", 0, X_STD },
    { "sot.tnsot", set_sot_tnsot, 0, "unspecified count: valid", 0, X_VALID },
    { "sot.tnsot", set_sot_tnsot, 2, "count 2 with one tile-part", 0, X_STD },
    { "plt.zplt", set_plt_zplt, 1, "first PLT index is not zero", 0, X_STD },
    { "plt.iplt.b0.bits", set_iplt_bits, 0, "packet length 0: PLT coverage short", 0, X_STD },
};

/* ------------------------------------------------------------------------ */
/* 5b. Rule mutants (structural)                                             */
/* ------------------------------------------------------------------------ */

typedef void (*RuleMutator)(Jp2Family *f, int box);

static void rule_second_cod(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    cs->segments.arr[3] = cs->segments.arr[2];           /* tile-part moves to 3 */
    cs->segments.arr[2] = cs->segments.arr[0];           /* duplicate COD */
    cs->segments.nCount = 4;
}
static void rule_second_qcd(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    cs->segments.arr[3] = cs->segments.arr[2];           /* tile-part moves to 3 */
    cs->segments.arr[2] = cs->segments.arr[1];           /* duplicate QCD */
    cs->segments.nCount = 4;
}
static void rule_no_qcd(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    cs->segments.arr[1] = cs->segments.arr[2];
    cs->segments.nCount = 2;
}
static void rule_qcd_after_sot(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    cs->segments.arr[3] = cs->segments.arr[1];            /* a QCD after the tile-part */
    cs->segments.nCount = 4;
}
static void rule_no_plt(Jp2Family *f, int box) {
    tp_of(f, box)->rest.headers.nCount = 0;
}
static void rule_precinct_count(Jp2Family *f, int box) {
    Cod *c = cod_of(f, box);
    c->spcod.precincts.nCount = c->spcod.levels + 2;    /* one byte too many */
}
static void rule_no_tile_part(Jp2Family *f, int box) {
    cs_of(f, box)->segments.nCount = 2;
}
static void rule_two_tile_parts(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    cod_of(f, box)->sgcod.layers = 2;
    cs->segments.arr[3] = cs->segments.arr[2];
    cs->segments.arr[3].tilePart.tpsot = 1;
    cs->segments.arr[2].tilePart.tnsot = 2;
    cs->segments.arr[3].tilePart.tnsot = 2;
    cs->segments.nCount = 4;                              /* valid: two tile-parts */
}
static void rule_tnsot_inconsistent(Jp2Family *f, int box) {
    rule_two_tile_parts(f, box);
    cs_of(f, box)->segments.arr[3].tilePart.tnsot = 3;   /* first said 2 */
}
static void rule_two_plt_markers(Jp2Family *f, int box) {
    TilePart *tp = tp_of(f, box);                          /* precinct base: two packets */
    TileSegment *first = &tp->rest.headers.arr[0];
    TileSegment *second = &tp->rest.headers.arr[tp->rest.headers.nCount++];
    *second = *first;                                      /* same code/kind, one entry each */
    first->plt.body.entries.nCount = 1;
    second->plt.body.zplt = 1;
    second->plt.body.entries.nCount = 1;
    second->plt.body.entries.arr[0] = first->plt.body.entries.arr[1];
}
static void rule_plt_boundaries(Jp2Family *f, int box) {
    static const int lengths[4] = {1, 127, 128, 129};
    Codestream *cs = cs_of(f, box);
    int part, packet;
    rule_two_tile_parts(f, box);
    cod_of(f, box)->sgcod.layers = 4;
    /* T.800 A.7.3 / Table A.36: complete lengths in separate PLT markers,
     * with Zplt restarting in the second tile-part. Payload is opaque to
     * this structural model; it is not an entropy-decoding fixture. */
    for (part = 0; part < 2; ++part) {
        TilePart *tp = &cs->segments.arr[2 + part].tilePart;
        tp->rest.headers.nCount = 2;
        tp->rest.headers.arr[1] = tp->rest.headers.arr[0];
        tp->rest.data.nCount = 0;
        for (packet = 0; packet < 2; ++packet) {
            int length = lengths[2 * part + packet];
            Plt *plt = &tp->rest.headers.arr[packet].plt.body;
            Iplt *entry = &plt->entries.arr[0];
            plt->zplt = packet;
            plt->entries.nCount = 1;
            memset(entry, 0, sizeof *entry);
            entry->b0.bits = length < 128 ? length : length >> 7;
            if (length >= 128) {
                entry->exist.b1 = 1;
                entry->b1.bits = length & 127;
            }
            memset(tp->rest.data.arr + tp->rest.data.nCount, 0, length);
            tp->rest.data.nCount += length;
        }
    }
}
static void rule_plt_second_part_short(Jp2Family *f, int box) {
    rule_plt_boundaries(f, box);
    cs_of(f, box)->segments.arr[3].tilePart.rest.headers.arr[1].plt.body.entries.arr[0].b1.bits = 0;
}
static void rule_plt_second_part_long(Jp2Family *f, int box) {
    rule_plt_boundaries(f, box);
    cs_of(f, box)->segments.arr[3].tilePart.rest.headers.arr[1].plt.body.entries.arr[0].b1.bits = 2;
}
static void rule_com_in_tile_header(Jp2Family *f, int box) {
    TilePart *tp = tp_of(f, box);
    TileSegment *ts = &tp->rest.headers.arr[tp->rest.headers.nCount++];
    ts->code = HV_COM;
    ts->exist.com = 1;
    ts->com.body.rcom = 1;
    ts->com.body.ccom.nCount = 2;
    ts->com.body.ccom.arr[0] = 'o';
    ts->com.body.ccom.arr[1] = 'k';
}
static void rule_tnsot_declared_late(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    rule_two_tile_parts(f, box);
    cs->segments.arr[2].tilePart.tnsot = 0;         /* first says "unspecified" */
    cs->segments.arr[3].tilePart.tnsot = 2;         /* second gives the count */
}
static void rule_tile_parts_n(Jp2Family *f, int box, int n) {
    Codestream *cs = cs_of(f, box);
    int i;
    cod_of(f, box)->sgcod.layers = n;
    for (i = 1; i < n; ++i) cs->segments.arr[2 + i] = cs->segments.arr[2];
    for (i = 0; i < n; ++i) {
        cs->segments.arr[2 + i].tilePart.tpsot = i;
        cs->segments.arr[2 + i].tilePart.tnsot = n;
    }
    cs->segments.nCount = 2 + n;
}
static void rule_tile_parts_64(Jp2Family *f, int box) { rule_tile_parts_n(f, box, 64); }
static void rule_jpx_jp2c_before_jpch(Jp2Family *f, int box) {
    TopBox *arr = f->boxes.arr;                       /* jP ftyp rreq jp2h jpch jpch jp2c jp2c */
    TopBox *jp2c = xcalloc(sizeof *jp2c);
    (void) box;
    *jp2c = arr[6];
    arr[6] = arr[5];
    arr[5] = arr[4];
    arr[4] = *jp2c;                                   /* jP ftyp rreq jp2h jp2c jpch jpch jp2c */
    free(jp2c);
}
static void rule_jpx_missing_jp2c(Jp2Family *f, int box) { (void) box; f->boxes.nCount = 7; }
static void rule_jpx_no_jpch(Jp2Family *f, int box) {
    (void) box;                                       /* jP ftyp rreq jp2h jp2c jp2c */
    f->boxes.arr[4] = f->boxes.arr[6];
    f->boxes.arr[5] = f->boxes.arr[7];
    f->boxes.nCount = 6;
}
static void rule_tile_parts_65(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    int i;
    cod_of(f, box)->sgcod.layers = 65;
    for (i = 1; i < 65; ++i) {
        cs->segments.arr[2 + i] = cs->segments.arr[2];
        cs->segments.arr[2 + i].tilePart.tpsot = i;
    }
    for (i = 0; i < 65; ++i) cs->segments.arr[2 + i].tilePart.tnsot = 65;
    cs->segments.nCount = 2 + 65;                         /* profile limit exceeded */
}
static void rule_com_segment(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    MainSegment *seg;
    cs->segments.arr[3] = cs->segments.arr[2];
    seg = &cs->segments.arr[2];
    memset(&seg->exist, 0, sizeof seg->exist);
    seg->code = HV_COM;
    seg->exist.com = 1;
    seg->com.body.rcom = 1;
    OCTETS(seg->com.body.ccom, "esajpip corpus", 14);
    cs->segments.nCount = 4;                              /* valid */
}
static void rule_iplt_five_bytes(Jp2Family *f, int box) {
    Iplt *e = &tp_of(f, box)->rest.headers.arr[0].plt.body.entries.arr[0];
    e->b0.bits = 0; e->exist.b1 = 1;
    e->b1.bits = 0; e->exist.b2 = 1;
    e->b2.bits = 0; e->exist.b3 = 1;
    e->b3.bits = 0; e->exist.b4 = 1;
    e->b4.bits = 1;                                        /* 5 bytes: profile max, valid */
}
static void rule_iplt_six_bytes(Jp2Family *f, int box) {
    Iplt *e = &tp_of(f, box)->rest.headers.arr[0].plt.body.entries.arr[0];
    rule_iplt_five_bytes(f, box);
    e->b4.bits = 0;
    e->exist.b5 = 1;
    e->b5.bits = 1;                                        /* non-minimal 6-byte encoding of value 1 */
}
static void set_iplt_ten_bytes(Iplt *e, uint8_t first,
                               uint8_t middle, uint8_t last) {
    memset(e, 0, sizeof *e);
    e->b0.bits = first;
    e->exist.b1 = 1; e->b1.bits = middle;
    e->exist.b2 = 1; e->b2.bits = middle;
    e->exist.b3 = 1; e->b3.bits = middle;
    e->exist.b4 = 1; e->b4.bits = middle;
    e->exist.b5 = 1; e->b5.bits = middle;
    e->exist.b6 = 1; e->b6.bits = middle;
    e->exist.b7 = 1; e->b7.bits = middle;
    e->exist.b8 = 1; e->b8.bits = middle;
    e->exist.b9 = 1; e->b9.bits = last;
}
static void rule_iplt_value_overflow(Jp2Family *f, int box) {
    Iplt *e = &tp_of(f, box)->rest.headers.arr[0].plt.body.entries.arr[0];
    set_iplt_ten_bytes(e, 2, 0, 1);                       /* 2^64 + 1 wraps to 1 */
}
static void rule_iplt_sum_overflow(Jp2Family *f, int box) {
    Plt *plt = &tp_of(f, box)->rest.headers.arr[0].plt.body;
    set_iplt_ten_bytes(&plt->entries.arr[0], 1, 127, 127); /* UINT64_MAX */
    plt->entries.arr[1].b0.bits = 3;                      /* wrapped sum equals data length 2 */
}
static void rule_trailing_zero_iplt(Jp2Family *f, int box) {
    Plt *plt = &tp_of(f, box)->rest.headers.arr[0].plt.body;
    Iplt *e = &plt->entries.arr[plt->entries.nCount++];    /* one Iplt more than packets */
    memset(e, 0, sizeof *e);
    e->b0.bits = 0;                                        /* value 0: tolerated by the server */
}
static void rule_merged_plt_packets(Jp2Family *f, int box) {
    Plt *plt = &tp_of(f, box)->rest.headers.arr[0].plt.body;
    plt->entries.arr[0].b0.bits = 2;                       /* two packets, one length */
    plt->entries.nCount = 1;
}
static void rule_middle_zero_iplt(Jp2Family *f, int box) {
    Plt *plt = &tp_of(f, box)->rest.headers.arr[0].plt.body;
    plt->entries.arr[2] = plt->entries.arr[1];
    memset(&plt->entries.arr[1], 0, sizeof plt->entries.arr[1]);
    plt->entries.nCount = 3;                              /* 1, 0, 1 for two packets */
}
static void rule_zero_logical_iplt(Jp2Family *f, int box) {
    TilePart *tp = tp_of(f, box);
    tp->rest.headers.arr[0].plt.body.entries.arr[0].b0.bits = 0;
    tp->rest.data.nCount = 0;                              /* zero length covers all data */
}
static void rule_extra_nonzero_iplt(Jp2Family *f, int box) {
    TilePart *tp = tp_of(f, box);
    Plt *plt = &tp->rest.headers.arr[0].plt.body;
    plt->entries.arr[1] = plt->entries.arr[0];
    plt->entries.nCount = 2;
    tp->rest.data.arr[tp->rest.data.nCount++] = 0;
}
static void rule_iplt_too_short(Jp2Family *f, int box) {
    TilePart *tp = tp_of(f, box);
    tp->rest.data.nCount += 1;                              /* one byte no PLT entry covers */
    tp->rest.data.arr[tp->rest.data.nCount - 1] = 0x00;
}
static void rule_tile_cod(Jp2Family *f, int box) {
    TilePart *tp = tp_of(f, box);
    TileSegment *ts = &tp->rest.headers.arr[tp->rest.headers.nCount++];
    ts->code = HV_COD;
    ts->exist.cod = 1;
    ts->cod.body = *cod_of(f, box);
}
static void rule_tile_qcd(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    TilePart *tp = tp_of(f, box);
    TileSegment *ts = &tp->rest.headers.arr[tp->rest.headers.nCount++];
    ts->code = HV_QCD;
    ts->exist.qcd = 1;
    ts->qcd.body = cs->segments.arr[1].qcd.body;
}
static void rule_tile_cod_twice(Jp2Family *f, int box) {
    TilePart *tp = tp_of(f, box);
    int n = tp->rest.headers.nCount, i;
    for (i = 0; i < 2; ++i) {                               /* two COD in the tile header */
        TileSegment *ts = &tp->rest.headers.arr[n + i];
        ts->code = HV_COD;
        ts->exist.cod = 1;
        ts->cod.body = *cod_of(f, box);
    }
    tp->rest.headers.nCount = n + 2;
}
static void rule_tile_cod_second_part(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    TilePart *tp;
    TileSegment *ts;
    rule_two_tile_parts(f, box);
    tp = &cs->segments.arr[3].tilePart;             /* COD in tile-part 1 */
    ts = &tp->rest.headers.arr[tp->rest.headers.nCount++];
    ts->code = HV_COD;
    ts->exist.cod = 1;
    ts->cod.body = *cod_of(f, box);
}
static void rule_packet_count_overflow(Jp2Family *f, int box) {
    Siz *s = &cs_of(f, box)->siz.body;                     /* 2^16 x 2^16 default precincts */
    s->xsiz = s->xtsiz = 2147483647;
    s->ysiz = s->ytsiz = 2147483647;
}
static void rule_iplt_too_long(Jp2Family *f, int box) {
    Iplt *e = &tp_of(f, box)->rest.headers.arr[0].plt.body.entries.arr[0];
    e->b0.bits = 2;                                        /* data holds 1 byte */
}
static void rule_subsampled(Jp2Family *f, int box) {
    cs_of(f, box)->siz.body.components.arr[0].xrsiz = 2;   /* profile: one shared precinct geometry */
}
static void add_main_opaque(Jp2Family *f, int box, int code) {
    Codestream *cs = cs_of(f, box);                        /* insert before the tile-part */
    MainSegment *seg;
    cs->segments.arr[3] = cs->segments.arr[2];
    seg = &cs->segments.arr[2];
    memset(&seg->exist, 0, sizeof seg->exist);
    seg->code = code;
    if (code == HV_COC) {
        seg->exist.coc = 1;
        OCTETS(seg->coc.body.data, "\x00\x00\x00", 3);
    } else if (code == HV_POC) {
        seg->exist.poc = 1;
        OCTETS(seg->poc.body.data, "\x00\x00\x00", 3);
    } else {
        seg->exist.ppm = 1;                                /* Zppm 0, Nppm 0 */
        OCTETS(seg->ppm.body.data, "\x00\x00\x00\x00\x00", 5);
    }
    cs->segments.nCount = 4;
}
static void rule_main_coc(Jp2Family *f, int box) { add_main_opaque(f, box, HV_COC); }
static void rule_main_poc(Jp2Family *f, int box) { add_main_opaque(f, box, HV_POC); }
static void rule_main_ppm(Jp2Family *f, int box) { add_main_opaque(f, box, HV_PPM); }
static void rule_ftyp_brand(Jp2Family *f, int box) {
    (void) box;
    memcpy(f->boxes.arr[1].payload.u.ftyp.brand.arr, "abcd", 4);           /* wrong brand */
}
static void rule_ftyp_compat(Jp2Family *f, int box) {
    (void) box;
    memcpy(f->boxes.arr[1].payload.u.ftyp.compat.arr[0].arr, "abcd", 4);   /* brand missing from list */
}
static void rule_ftyp_two_compat(Jp2Family *f, int box) {
    Ftyp *ftyp = &f->boxes.arr[1].payload.u.ftyp;                          /* brand second: valid */
    (void) box;
    ftyp->compat.nCount = 2;
    ftyp->compat.arr[1] = ftyp->compat.arr[0];
    memcpy(ftyp->compat.arr[0].arr, "jpxb", 4);
}
static void rule_jp2c_in_jpch(Jp2Family *f, int box) {
    Superbox *sb = &f->boxes.arr[4].payload.u.jpch;        /* jpx-embedded: first jpch */
    InnerBox *c = &sb->children.arr[sb->children.nCount++];
    (void) box;
    c->payload.kind = InnerPayload_jp2c_PRESENT;
    OCTETS(c->payload.u.jp2c.data, "\x00", 1);
}
static void rule_missing_rreq(Jp2Family *f, int box) {
    int i;
    (void) box;
    for (i = 3; i < f->boxes.nCount; ++i) f->boxes.arr[i - 1] = f->boxes.arr[i];
    f->boxes.nCount--;
}
static void rule_unknown_box(Jp2Family *f, int box) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    (void) box;
    b->payload.kind = TopPayload_other_PRESENT;
    OCTETS(b->payload.u.other.data, "\x01", 1);
}
static void rule_missing_signature(Jp2Family *f, int box) {
    int i;
    (void) box;
    for (i = 1; i < f->boxes.nCount; ++i) f->boxes.arr[i - 1] = f->boxes.arr[i];
    f->boxes.nCount--;
}
static void rule_two_tiles(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    cs->siz.body.xtsiz = BASE_W / 2;                       /* two tiles, one tile-part each */
    cs->segments.arr[3] = cs->segments.arr[2];
    cs->segments.arr[3].tilePart.isot = 1;
    cs->segments.nCount = 4;
}
static void rule_tile_cod_mct(Jp2Family *f, int box) {
    rule_tile_cod(f, box);                                 /* one component */
    tp_of(f, box)->rest.headers.arr[1].cod.body.sgcod.mct = 1;
}
static void rule_jp2_empty_ftbl(Jp2Family *f, int box) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];         /* no flst */
    (void) box;
    b->payload.kind = TopPayload_ftbl_PRESENT;
    b->payload.u.ftbl.children.nCount = 0;
}
static void rule_two_jp2c(Jp2Family *f, int box) {
    f->boxes.arr[f->boxes.nCount] = f->boxes.arr[box];
    f->boxes.nCount++;
}

typedef struct {
    const char *name;
    RuleMutator apply;
    const char *note;
    int only_kind;         /* 0 = any base with a codestream, CF_JP2 / CF_JPX otherwise */
    int needs_precincts;   /* 1 = apply to the explicit-precinct base (two packets) */
    Expect expect;
} RuleMutant;

/* Both rule tables use array positions in fixture names; append new entries. */
static const RuleMutant rule_mutants[] = {
    { "codestream.one-cod-before-sot", rule_second_cod, "two COD in main header", 0, 0, X_STD },
    { "codestream.one-qcd-before-sot", rule_second_qcd, "two QCD in main header", 0, 0, X_STD },
    { "codestream.one-qcd-before-sot", rule_no_qcd, "no QCD", 0, 0, X_STD },
    { "codestream.segment-after-sot", rule_qcd_after_sot, "QCD after the tile-part data", 0, 0, X_STD },
    { "codestream.no-plt", rule_no_plt, "no PLT: standard valid, profile invalid", 0, 0, X_PROF },
    { "cod.precincts-count", rule_precinct_count, "levels+2 precinct bytes", 0, 0, X_STD },
    { "codestream.no-tile-part", rule_no_tile_part, "main header only", 0, 0, X_STD },
    { "sot.two-tile-parts", rule_two_tile_parts, "two tile-parts: valid", 0, 0, X_VALID },
    { "sot.tnsot-late", rule_tnsot_declared_late, "TNsot 0 then 2: valid", 0, 0, X_VALID },
    { "codestream.tile-part-limit", rule_tile_parts_64, "64 tile-parts: profile maximum, valid", 0, 0, X_VALID },
    { "plt.two-markers", rule_two_plt_markers, "packet lengths split over two PLT: valid", 0, 1, X_VALID },
    { "tile.header-marker", rule_com_in_tile_header, "COM in the tile-part header: profile invalid", 0, 0, X_PROF },
    { "jpx.box-order", rule_jpx_jp2c_before_jpch, "jp2c before the jpch boxes: valid (counts match)", CF_JPX, 0, X_VALID },
    { "jpx.reader-requirements", rule_missing_rreq, "no rreq box: standard invalid, profile accepted", CF_JPX, 0, X_STD },
    { "file.unknown-box", rule_unknown_box, "unknown top-level box: valid and skipped", 0, 0, X_VALID },
    { "jpx.codestream-count", rule_jpx_missing_jp2c, "two jpch, one jp2c (T.801 M.11.6)", CF_JPX, 0, X_STD },
    { "plt.trailing-zero", rule_trailing_zero_iplt, "extra zero Iplt after the last packet: standard invalid, profile accepted for deployed files", 0, 0, X_STD },
    { "jpx.no-jpch", rule_jpx_no_jpch, "jp2c boxes without jpch: profile invalid", CF_JPX, 0, X_PROF },
    { "jpch.nested-jp2c", rule_jp2c_in_jpch, "jp2c inside a jpch superbox", CF_JPX, 0, X_STD },
    { "sot.tnsot-inconsistent", rule_tnsot_inconsistent, "second SOT declares 3 tile-parts, first 2", 0, 0, X_STD },
    { "codestream.tile-part-limit", rule_tile_parts_65, "65 tile-parts: profile invalid", 0, 0, X_PROF },
    { "main.com", rule_com_segment, "COM segment: valid", 0, 0, X_VALID },
    { "plt.iplt-five-bytes", rule_iplt_five_bytes, "5-byte Iplt encoding value 1: valid", 0, 0, X_VALID },
    { "plt.sum-exceeds-data", rule_iplt_too_long, "packet length beyond tile-part data", 0, 0, X_STD },
    { "plt.sum-short", rule_iplt_too_short, "tile-part byte no PLT entry covers", 0, 0, X_STD },
    { "tile.header-coding-default", rule_tile_cod, "COD in the first tile-part: profile invalid", 0, 0, X_PROF },
    { "tile.header-coding-default", rule_tile_qcd, "QCD in the first tile-part: profile invalid", 0, 0, X_PROF },
    { "tile.cod-once", rule_tile_cod_twice, "two COD in the tile-part header", 0, 0, X_STD },
    { "tile.cod-first-part", rule_tile_cod_second_part, "COD in the second tile-part", 0, 0, X_STD },
    { "codestream.packet-count", rule_packet_count_overflow, "2^32 packets: profile invalid", 0, 0, X_PROF },
    { "plt.iplt-six-bytes", rule_iplt_six_bytes, "6-byte Iplt encoding value 1: valid", 0, 0, X_VALID },
    { "siz.component-sampling", rule_subsampled, "2:1 sampling: profile invalid", 0, 0, X_PROF },
    { "main.packet-layout-override", rule_main_coc, "COC in the main header: profile invalid", 0, 0, X_PROF },
    { "main.packet-layout-override", rule_main_poc, "POC in the main header: profile invalid", 0, 0, X_PROF },
    { "file.ftyp-brand", rule_ftyp_brand, "ftyp brand 'abcd'", 0, 0, X_STD },
    { "file.ftyp-compatibility", rule_ftyp_compat, "brand absent from the compatibility list", 0, 0, X_STD },
    { "file.ftyp-compatibility", rule_ftyp_two_compat, "brand second in the compatibility list: valid", 0, 0, X_VALID },
    { "file.signature", rule_missing_signature, "no jP box (T.800 I.4)", CF_JP2, 0, X_STD },
    { "jp2.one-codestream", rule_two_jp2c, "two jp2c in .jp2", CF_JP2, 0, X_PROF },
    { "plt.boundaries", rule_plt_boundaries, "T.800 A.7.3: lengths 1,127,128,129 across PLT and tile-part boundaries", 0, 0, X_VALID },
    { "plt.second-part-short", rule_plt_second_part_short, "second tile-part PLT sum one byte short", 0, 0, X_STD },
    { "plt.second-part-long", rule_plt_second_part_long, "second tile-part PLT sum one byte too long", 0, 0, X_STD },
    { "plt.packet-count", rule_merged_plt_packets, "two packet lengths merged into one entry with unchanged data", 0, 1, X_STD },
    { "plt.padding-position", rule_middle_zero_iplt, "zero Iplt between two logical packets", 0, 1, X_STD },
    { "plt.zero-length", rule_zero_logical_iplt, "zero Iplt for the only logical packet", 0, 0, X_STD },
    { "plt.packet-count", rule_extra_nonzero_iplt, "nonzero Iplt beyond the only logical packet", 0, 0, X_STD },
    { "plt.value-overflow", rule_iplt_value_overflow, "Iplt value 2^64+1 wraps to data length 1", 0, 0, X_STD },
    { "plt.sum-overflow", rule_iplt_sum_overflow, "Iplt lengths UINT64_MAX+3 wrap to data length 2", 0, 1, X_STD },
    { "main.packet-headers-moved", rule_main_ppm, "PPM in the main header: profile invalid", 0, 0, X_PROF },
    { "sot.two-tiles", rule_two_tiles, "two tiles, TPsot 0 and TNsot 1 in each: standard valid, profile invalid", 0, 0, X_PROF },
    { "siz.mct-components", rule_tile_cod_mct, "MCT in a tile-part COD with one component", 0, 0, X_STD },
    { "ftbl.one-flst", rule_jp2_empty_ftbl, "empty ftbl in a .jp2: standard invalid, opaque to the profile (ReadJP2)", CF_JP2, 0, X_STD },
};

/* Rule mutants specific to linked JPX (need the linked base). */
static void rule_dr_zero(Jp2Family *f, int box) { (void) box; f->boxes.arr[6].payload.u.ftbl.children.arr[0].payload.u.flst.fragments.arr[0].dr = 0; }
static void rule_dr_out_of_range(Jp2Family *f, int box) { (void) box; f->boxes.arr[6].payload.u.ftbl.children.arr[0].payload.u.flst.fragments.arr[0].dr = 3; }
static void rule_two_flst(Jp2Family *f, int box) {
    Superbox *sb = &f->boxes.arr[6].payload.u.ftbl;
    (void) box;
    sb->children.arr[1] = sb->children.arr[0];
    sb->children.nCount = 2;
}
static void rule_ndr_mismatch(Jp2Family *f, int box) { (void) box; f->boxes.arr[8].payload.u.dtbl.ndr = 3; }
static void rule_url_scheme(Jp2Family *f, int box) {
    (void) box;
    strcpy((char *) f->boxes.arr[8].payload.u.dtbl.references.arr[0].payload.u.url.loc, "http://x/frame1.jp2");
}
static void rule_url_jpx_target(Jp2Family *f, int box) {
    (void) box;
    strcpy((char *) f->boxes.arr[8].payload.u.dtbl.references.arr[0].payload.u.url.loc, "file://./frame1.jpx");
}
static void rule_url_version(Jp2Family *f, int box) { (void) box; f->boxes.arr[8].payload.u.dtbl.references.arr[0].payload.u.url.vers = 1; }
/* A third codestream as jp2c beside the two ftbl ones, with its own jpch so
 * that the codestream count still matches and mixing is the only violation. */
static void rule_mixed_linked_embedded(Jp2Family *f, int box) {
    (void) box;
    add_jp2c(f, BASE_W, BASE_H, 0, 0, 1);
    add_jpch(f);
}
static void rule_no_dtbl(Jp2Family *f, int box) { (void) box; f->boxes.nCount = 8; }
static void rule_two_dtbl(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[9] = f->boxes.arr[8];                    /* second top-level dtbl */
    f->boxes.nCount = 10;
}
static void rule_fragment_early(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[6].payload.u.ftbl.children.arr[0].payload.u.flst.fragments.arr[0].off--;
}
static void rule_fragment_late(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[6].payload.u.ftbl.children.arr[0].payload.u.flst.fragments.arr[0].off++;
}
static void rule_fragment_short(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[6].payload.u.ftbl.children.arr[0].payload.u.flst.fragments.arr[0].len--;
}
static void rule_fragment_long(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[6].payload.u.ftbl.children.arr[0].payload.u.flst.fragments.arr[0].len++;
}
static void rule_missing_companion(Jp2Family *f, int box) {
    (void) box;
    strcpy((char *) f->boxes.arr[8].payload.u.dtbl.references.arr[0].payload.u.url.loc,
           "file://./jpx-missing-frame.jp2");
}

static void rule_flst_in_jpch(Jp2Family *f, int box) {
    Superbox *jpch = &f->boxes.arr[4].payload.u.jpch;      /* a copy of the first flst */
    (void) box;
    jpch->children.arr[jpch->children.nCount++] = f->boxes.arr[6].payload.u.ftbl.children.arr[0];
}
static void rule_url_in_ftbl(Jp2Family *f, int box) {
    Superbox *ftbl = &f->boxes.arr[6].payload.u.ftbl;      /* a copy of the first url */
    (void) box;
    ftbl->children.arr[ftbl->children.nCount++] =
        f->boxes.arr[8].payload.u.dtbl.references.arr[0];
}

/* Appended entries only: array positions are in fixture names. */
static const RuleMutant linked_rule_mutants[] = {
    { "jpx.reader-requirements", rule_missing_rreq, "no rreq box: standard invalid, profile accepted", CF_JPX, 0, X_STD },
    { "file.unknown-box", rule_unknown_box, "unknown top-level box: valid and skipped", CF_JPX, 0, X_VALID },
    { "flst.dr-external", rule_dr_zero, "DR = 0 (this file): profile invalid", CF_JPX, 0, X_PROF },
    { "flst.dr-range", rule_dr_out_of_range, "DR = ndr+1", CF_JPX, 0, X_STD },
    { "ftbl.one-flst", rule_two_flst, "two flst in one ftbl", CF_JPX, 0, X_STD },
    { "dtbl.ndr-count", rule_ndr_mismatch, "NDR = 3 with 2 url boxes", CF_JPX, 0, X_STD },
    { "url.file-scheme", rule_url_scheme, "http URL: profile invalid", CF_JPX, 0, X_PROF },
    { "url.version", rule_url_version, "VERS = 1: profile invalid", CF_JPX, 0, X_PROF },
    { "url.jp2-target", rule_url_jpx_target, "link to a .jpx: profile invalid", CF_JPX, 0, X_PROF },
    { "jpx.mixed-sources", rule_mixed_linked_embedded, "jp2c next to ftbl: profile invalid", CF_JPX, 0, X_PROF },
    { "jpx.linked-shape", rule_no_dtbl, "no dtbl", CF_JPX, 0, X_STD },
    { "jpx.one-dtbl", rule_two_dtbl, "two dtbl boxes (T.801 M.11.2)", CF_JPX, 0, X_STD },
    { "flst.source-extent", rule_fragment_early, "fragment starts one byte before companion codestream", CF_JPX, 0, X_PROF },
    { "flst.source-extent", rule_fragment_late, "fragment starts one byte after companion codestream", CF_JPX, 0, X_PROF },
    { "flst.source-extent", rule_fragment_short, "fragment omits final companion codestream byte", CF_JPX, 0, X_PROF },
    { "flst.source-extent", rule_fragment_long, "fragment extends past companion codestream", CF_JPX, 0, X_PROF },
    { "url.missing-companion", rule_missing_companion, "structurally valid JPX with unavailable companion", CF_JPX, 0, X_PROF },
    { "box.flst-placement", rule_flst_in_jpch, "flst inside a jpch (T.801 M.11.3: in ftbl)", CF_JPX, 0, X_STD },
    { "box.url-placement", rule_url_in_ftbl, "url inside an ftbl (T.801 M.11.2: in dtbl)", CF_JPX, 0, X_STD },
};

/* ------------------------------------------------------------------------ */
/* 5b'. Header box mutants (T.800 I.5.3, T.801 M.11.5 to M.11.7)             */
/* ------------------------------------------------------------------------ */

/* A codestream mutant keeps the image header in step with its SIZ, so that
 * it violates what it means to and no header rule: the ihdr of jp2h (JP2)
 * or of codestream k's jpch (JPX). A SIZ outside what an ihdr can state
 * (an empty image, a reserved depth, components that differ) is left: the
 * codestream rules reject it first. */
static void sync_ihdr(InnerBox *c, const Siz *s) {
    Ihdr *ihdr = &c->payload.u.ihdr;
    int i;
    if (c->payload.kind != InnerPayload_ihdr_PRESENT ||
        s->ysiz <= s->yosiz || s->xsiz <= s->xosiz || s->ysiz - s->yosiz > 4294967295u ||
        s->xsiz - s->xosiz > 4294967295u || s->csiz < 1 || s->csiz > 16384 ||
        s->components.nCount < 1)
        return;
    for (i = 0; i < s->components.nCount; ++i)
        if (s->components.arr[i].depthMinus1 > 37 ||
            s->components.arr[i].depthMinus1 != s->components.arr[0].depthMinus1 ||
            s->components.arr[i].isSigned != s->components.arr[0].isSigned)
            return;
    ihdr->height = s->ysiz - s->yosiz;
    ihdr->width = s->xsiz - s->xosiz;
    ihdr->nc = s->csiz;
    ihdr->bpc = s->components.arr[0].depthMinus1 | (s->components.arr[0].isSigned ? 128 : 0);
}

static void sync_headers(Jp2Family *f) {
    Superbox *header = NULL;
    int i, k, stream = 0;
    for (i = 0; i < f->boxes.nCount; ++i) {
        TopPayload *p = &f->boxes.arr[i].payload;
        if (p->kind == TopPayload_jp2h_PRESENT && header == NULL) header = &p->u.jp2h;
        if (p->kind != TopPayload_jp2c_PRESENT) continue;
        if (stream++ == 0 && header != NULL && header->children.nCount > 0)
            sync_ihdr(&header->children.arr[0], &p->u.jp2c.siz.body);
        for (k = 0; k < f->boxes.nCount; ++k) {
            TopPayload *q = &f->boxes.arr[k].payload;
            int seen = 0, j;
            if (q->kind != TopPayload_jpch_PRESENT) continue;
            for (j = 0; j < k; ++j) seen += f->boxes.arr[j].payload.kind == TopPayload_jpch_PRESENT;
            if (seen == stream - 1 && q->u.jpch.children.nCount > 0)
                sync_ihdr(&q->u.jpch.children.arr[0], &p->u.jp2c.siz.body);
        }
    }
}

/* The JP2 base is jP ftyp jp2h jp2c; the embedded JPX base jP ftyp rreq
 * jp2h jpch jpch jp2c jp2c, each jpch holding an ihdr. */
static Superbox *jp2h_of(Jp2Family *f) { return &f->boxes.arr[2].payload.u.jp2h; }
static Superbox *jpx_jp2h_of(Jp2Family *f) { return &f->boxes.arr[3].payload.u.jp2h; }
static Superbox *jpch_of(Jp2Family *f) { return &f->boxes.arr[4].payload.u.jpch; }

/* Inserts a child at position `at` of a header box. */
static InnerBox *insert_child(Superbox *sb, int at) {
    int i;
    for (i = sb->children.nCount; i > at; --i) sb->children.arr[i] = sb->children.arr[i - 1];
    sb->children.nCount++;
    memset(&sb->children.arr[at], 0, sizeof sb->children.arr[at]);
    return &sb->children.arr[at];
}
static InnerBox *append_child(Superbox *sb) { return insert_child(sb, sb->children.nCount); }

static void set_bpcc(InnerBox *c, int n, int depth) {
    int i;
    c->payload.kind = InnerPayload_bpcc_PRESENT;
    c->payload.u.bpcc.depths.nCount = n;
    for (i = 0; i < n; ++i) c->payload.u.bpcc.depths.arr[i] = depth;
}

/* A palette of NE 2 entries in `columns` unsigned 8-bit columns. */
static void set_pclr_columns(InnerBox *c, int columns) {
    Pclr *p = &c->payload.u.pclr;
    int i;
    c->payload.kind = InnerPayload_pclr_PRESENT;
    p->header.ne = 2;
    p->header.depths.nCount = columns;
    for (i = 0; i < columns; ++i) {
        p->header.depths.arr[i] = 7;
        p->entries.arr[i] = 0x00;
        p->entries.arr[columns + i] = 0xFF;
    }
    p->entries.nCount = 2 * columns;
}
static void set_pclr(InnerBox *c) { set_pclr_columns(c, 1); }

/* Channel i from palette column i of component 0. */
static void set_cmap_columns(InnerBox *c, int columns) {
    int i;
    c->payload.kind = InnerPayload_cmap_PRESENT;
    c->payload.u.cmap.entries.nCount = columns;
    for (i = 0; i < columns; ++i) {
        c->payload.u.cmap.entries.arr[i].cmp = 0;
        c->payload.u.cmap.entries.arr[i].mtyp = 1;
        c->payload.u.cmap.entries.arr[i].pcol = i;
    }
}

static void set_cmap(InnerBox *c, int cmp, int mtyp, int pcol) {
    CmapEntry *e = &c->payload.u.cmap.entries.arr[0];
    c->payload.kind = InnerPayload_cmap_PRESENT;
    c->payload.u.cmap.entries.nCount = 1;
    e->cmp = cmp;
    e->mtyp = mtyp;
    e->pcol = pcol;
}

static void set_cdef(InnerBox *c, int n, int cn, int typ, int asoc) {
    int i;
    c->payload.kind = InnerPayload_cdef_PRESENT;
    c->payload.u.cdef.entries.nCount = n;
    for (i = 0; i < n; ++i) {
        c->payload.u.cdef.entries.arr[i].cn = cn;
        c->payload.u.cdef.entries.arr[i].typ = typ;
        c->payload.u.cdef.entries.arr[i].asoc = asoc;
    }
}

/* A Resolution box: resc (72 dpi, 2835 grid points per metre, as 2835/1)
 * and resd children as given; `other` adds an unknown box. */
static void set_res(InnerBox *c, int resc, int resd, int other) {
    Res *r = &c->payload.u.res;
    int n = 0, i;
    c->payload.kind = InnerPayload_res_PRESENT;
    for (i = 0; i < resc + resd; ++i) {
        ResBox *b = &r->children.arr[n++];
        Resolution *v;
        b->payload.kind = i < resc ? resc_PRESENT : resd_PRESENT;
        v = i < resc ? &b->payload.u.resc : &b->payload.u.resd;
        v->vn = 2835; v->vd = 1; v->hn = 2835; v->hd = 1; v->ve = 0; v->he = 0;
    }
    if (other) {
        r->children.arr[n].payload.kind = ResPayload_other_PRESENT;
        OCTETS(r->children.arr[n].payload.u.other.data, "\x01", 1);
        n++;
    }
    r->children.nCount = n;
}

static void set_xml(InnerBox *c) {
    c->payload.kind = InnerPayload_xml_PRESENT;
    OCTETS(c->payload.u.xml.data, "<a/>", 4);
}

/* JP2 mutants: jp2h is box 2, holding ihdr and a greyscale colr. */
static void hdr_palette(Jp2Family *f, int box) {
    (void) box;
    set_pclr(append_child(jp2h_of(f)));
    set_cmap(append_child(jp2h_of(f)), 0, 1, 0);
}
static void hdr_resolution(Jp2Family *f, int box) { (void) box; set_res(append_child(jp2h_of(f)), 1, 1, 0); }
static void hdr_other_child(Jp2Family *f, int box) { (void) box; set_xml(append_child(jp2h_of(f))); }
static void hdr_second_colr(Jp2Family *f, int box) { (void) box; set_colr(append_child(jp2h_of(f)), 1, 17); }
/* Three channels from a three-column palette: grey and two auxiliary
 * channels of unspecified type, which may share the pair (65535, 65535). */
static void hdr_cdef_unspecified(Jp2Family *f, int box) {
    InnerBox *c;
    (void) box;
    set_pclr_columns(append_child(jp2h_of(f)), 3);
    set_cmap_columns(append_child(jp2h_of(f)), 3);
    c = append_child(jp2h_of(f));
    set_cdef(c, 3, 0, 65535, 65535);
    c->payload.u.cdef.entries.arr[0].typ = 0;
    c->payload.u.cdef.entries.arr[0].asoc = 1;
    c->payload.u.cdef.entries.arr[1].cn = 1;
    c->payload.u.cdef.entries.arr[2].cn = 2;
}
/* Grey and opacity from a two-column palette. */
static void hdr_cdef_opacity(Jp2Family *f, int box) {
    InnerBox *c;
    (void) box;
    set_pclr_columns(append_child(jp2h_of(f)), 2);
    set_cmap_columns(append_child(jp2h_of(f)), 2);
    c = append_child(jp2h_of(f));
    set_cdef(c, 2, 0, 0, 1);
    c->payload.u.cdef.entries.arr[1].cn = 1;
    c->payload.u.cdef.entries.arr[1].typ = 1;
    c->payload.u.cdef.entries.arr[1].asoc = 0;
}
static void hdr_no_jp2h(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[2] = f->boxes.arr[3];
    f->boxes.nCount = 3;
}
static void hdr_two_jp2h(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[4] = f->boxes.arr[3];
    f->boxes.arr[3] = f->boxes.arr[2];
    f->boxes.nCount = 5;
}
static void hdr_jp2h_after_jp2c(Jp2Family *f, int box) {
    TopBox *jp2h = xcalloc(sizeof *jp2h);
    (void) box;
    *jp2h = f->boxes.arr[2];
    f->boxes.arr[2] = f->boxes.arr[3];
    f->boxes.arr[3] = *jp2h;
    free(jp2h);
}
static void hdr_no_jp2c(Jp2Family *f, int box) { (void) box; f->boxes.nCount = 3; }
static void hdr_colr_first(Jp2Family *f, int box) {
    Superbox *sb = jp2h_of(f);
    InnerBox *ihdr = xcalloc(sizeof *ihdr);
    (void) box;
    *ihdr = sb->children.arr[0];
    sb->children.arr[0] = sb->children.arr[1];
    sb->children.arr[1] = *ihdr;
    free(ihdr);
}
static void hdr_no_colr(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.nCount = 1; }
static void hdr_colr_split(Jp2Family *f, int box) {
    (void) box;
    set_xml(append_child(jp2h_of(f)));
    set_colr(append_child(jp2h_of(f)), 1, 16);
}
static void hdr_bpcc_not_varying(Jp2Family *f, int box) { (void) box; set_bpcc(append_child(jp2h_of(f)), 1, 7); }
static void hdr_bpc_255_no_bpcc(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[0].payload.u.ihdr.bpc = 255; }
static void hdr_two_bpcc(Jp2Family *f, int box) {
    (void) box;
    jp2h_of(f)->children.arr[0].payload.u.ihdr.bpc = 255;
    set_bpcc(append_child(jp2h_of(f)), 1, 7);
    set_bpcc(append_child(jp2h_of(f)), 1, 7);
}
static void hdr_bpcc_count(Jp2Family *f, int box) {
    (void) box;
    jp2h_of(f)->children.arr[0].payload.u.ihdr.bpc = 255;
    set_bpcc(append_child(jp2h_of(f)), 2, 7);
}
static void hdr_bpcc_depth(Jp2Family *f, int box) {
    (void) box;
    jp2h_of(f)->children.arr[0].payload.u.ihdr.bpc = 255;
    set_bpcc(append_child(jp2h_of(f)), 1, 8);
}
static void hdr_ihdr_height(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[0].payload.u.ihdr.height = BASE_H + 1; }
static void hdr_ihdr_width(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[0].payload.u.ihdr.width = BASE_W + 1; }
static void hdr_ihdr_nc(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[0].payload.u.ihdr.nc = 2; }
static void hdr_ihdr_bpc(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[0].payload.u.ihdr.bpc = 8; }
static void hdr_ihdr_signed(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[0].payload.u.ihdr.bpc = 135; }
static void hdr_ihdr_c(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[0].payload.u.ihdr.c = 0; }
static void hdr_ihdr_unkc(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[0].payload.u.ihdr.unkc = 2; }
static void hdr_ihdr_nc_zero(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[0].payload.u.ihdr.nc = 0; }
static void hdr_ihdr_bpc_38(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[0].payload.u.ihdr.bpc = 38; }
static void hdr_colr_method(Jp2Family *f, int box) { (void) box; set_colr(&jp2h_of(f)->children.arr[1], 3, 0); }
static void hdr_colr_enumcs(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.arr[1].payload.u.colr.header.enumcs = 12; }
static void hdr_colr_enumcs_length(Jp2Family *f, int box) {
    (void) box;
    OCTETS(jp2h_of(f)->children.arr[1].payload.u.colr.rest, "\x00", 1);
}
static void hdr_colr_prec_approx(Jp2Family *f, int box) {
    (void) box;
    jp2h_of(f)->children.arr[1].payload.u.colr.header.prec = -1;
    jp2h_of(f)->children.arr[1].payload.u.colr.header.approx = 1;
}
static void hdr_pclr_no_cmap(Jp2Family *f, int box) { (void) box; set_pclr(append_child(jp2h_of(f))); }
static void hdr_cmap_no_pclr(Jp2Family *f, int box) { (void) box; set_cmap(append_child(jp2h_of(f)), 0, 0, 0); }
static void hdr_two_pclr(Jp2Family *f, int box) {
    hdr_palette(f, box);
    set_pclr(append_child(jp2h_of(f)));
}
static void hdr_two_cmap(Jp2Family *f, int box) {
    hdr_palette(f, box);
    set_cmap(append_child(jp2h_of(f)), 0, 1, 0);
}
static void hdr_pclr_entries(Jp2Family *f, int box) {
    hdr_palette(f, box);
    jp2h_of(f)->children.arr[2].payload.u.pclr.entries.nCount = 1;
}
static void hdr_pclr_ne_zero(Jp2Family *f, int box) {
    hdr_palette(f, box);
    jp2h_of(f)->children.arr[2].payload.u.pclr.header.ne = 0;
    jp2h_of(f)->children.arr[2].payload.u.pclr.entries.nCount = 0;
}
static void hdr_cmap_pcol_zero(Jp2Family *f, int box) {
    hdr_palette(f, box);
    set_cmap(&jp2h_of(f)->children.arr[3], 0, 0, 1);
}
static void hdr_cmap_component(Jp2Family *f, int box) {
    hdr_palette(f, box);
    set_cmap(&jp2h_of(f)->children.arr[3], 1, 1, 0);
}
static void hdr_cmap_palette_column(Jp2Family *f, int box) {
    hdr_palette(f, box);
    set_cmap(&jp2h_of(f)->children.arr[3], 0, 1, 1);
}
static void hdr_cmap_mtyp(Jp2Family *f, int box) {
    hdr_palette(f, box);
    set_cmap(&jp2h_of(f)->children.arr[3], 0, 2, 0);
}
static void hdr_cdef_pairs(Jp2Family *f, int box) { (void) box; set_cdef(append_child(jp2h_of(f)), 2, 0, 0, 1); }
static void hdr_cdef_channel(Jp2Family *f, int box) { (void) box; set_cdef(append_child(jp2h_of(f)), 1, 1, 0, 1); }
static void hdr_cdef_typ(Jp2Family *f, int box) { (void) box; set_cdef(append_child(jp2h_of(f)), 1, 0, 3, 1); }
static void hdr_two_cdef(Jp2Family *f, int box) {
    (void) box;
    set_cdef(append_child(jp2h_of(f)), 1, 0, 0, 1);
    set_cdef(append_child(jp2h_of(f)), 1, 0, 0, 1);
}
static void hdr_two_res(Jp2Family *f, int box) {
    (void) box;
    set_res(append_child(jp2h_of(f)), 1, 0, 0);
    set_res(append_child(jp2h_of(f)), 0, 1, 0);
}
static void hdr_res_two_resc(Jp2Family *f, int box) { (void) box; set_res(append_child(jp2h_of(f)), 2, 0, 0); }
static void hdr_res_empty(Jp2Family *f, int box) { (void) box; set_res(append_child(jp2h_of(f)), 0, 0, 1); }
static void hdr_res_other(Jp2Family *f, int box) { (void) box; set_res(append_child(jp2h_of(f)), 0, 1, 1); }
static void hdr_res_zero(Jp2Family *f, int box) {
    (void) box;
    set_res(append_child(jp2h_of(f)), 1, 0, 0);
    jp2h_of(f)->children.arr[2].payload.u.res.children.arr[0].payload.u.resc.vd = 0;
}

/* JPX mutants on the embedded base. */
static void remove_box(Jp2Family *f, int at) {
    int i;
    for (i = at + 1; i < f->boxes.nCount; ++i) f->boxes.arr[i - 1] = f->boxes.arr[i];
    f->boxes.nCount--;
}
static void hdr_jpx_no_ihdr(Jp2Family *f, int box) {
    (void) box;
    jpch_of(f)->children.nCount = 0;
    remove_box(f, 3);
}
static void hdr_jpx_defaults(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[4].payload.u.jpch.children.nCount = 0;
    f->boxes.arr[5].payload.u.jpch.children.nCount = 0;
}
static void hdr_jpx_no_jp2h(Jp2Family *f, int box) { (void) box; remove_box(f, 3); }
static void hdr_jpx_late_jp2h(Jp2Family *f, int box) {
    (void) box;
    remove_box(f, 3);
    add_jp2h(f, BASE_W, BASE_H);
}
static void hdr_jpx_two_ihdr(Jp2Family *f, int box) { (void) box; set_ihdr(append_child(jpch_of(f)), BASE_W, BASE_H); }
static void hdr_jpx_ihdr_width(Jp2Family *f, int box) { (void) box; jpch_of(f)->children.arr[0].payload.u.ihdr.width = BASE_W + 1; }
static void hdr_jpx_palette(Jp2Family *f, int box) {
    (void) box;
    set_pclr(append_child(jpch_of(f)));
    set_cmap(append_child(jpch_of(f)), 0, 1, 0);
}
static void hdr_jpx_pclr_no_cmap(Jp2Family *f, int box) { (void) box; set_pclr(append_child(jpch_of(f))); }
static void hdr_jpx_two_enumerated(Jp2Family *f, int box) {
    (void) box;
    set_colr(append_child(jpx_jp2h_of(f)), 1, 16);
}
static void hdr_jpx_jplh(Jp2Family *f, int box) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];     /* after the codestreams */
    (void) box;
    b->payload.kind = TopPayload_jplh_PRESENT;
    b->payload.u.jplh.children.nCount = 0;
    set_res(append_child(&b->payload.u.jplh), 0, 1, 0);
}
/* A byte after the fields of a box whose layout ends before its end. */
static void hdr_ihdr_extent(Jp2Family *f, int box) {
    (void) box;
    OCTETS(jp2h_of(f)->children.arr[0].payload.u.ihdr.extra, "\x00", 1);
}
static void hdr_cdef_extent(Jp2Family *f, int box) {
    hdr_cdef_opacity(f, box);
    OCTETS(jp2h_of(f)->children.arr[4].payload.u.cdef.extra, "\x00", 1);
}
static void hdr_res_extent(Jp2Family *f, int box) {
    (void) box;
    set_res(append_child(jp2h_of(f)), 1, 0, 0);
    OCTETS(jp2h_of(f)->children.arr[2].payload.u.res.children.arr[0].payload.u.resc.extra, "\x00", 1);
}

/* The Reader Requirements box is box 2 of a JPX base. */
static void hdr_rreq_mask_length(Jp2Family *f, int box, int ml) {
    Rreq *r = &f->boxes.arr[2].payload.u.rreq;
    int i;
    (void) box;
    set_mask(&r->fuam, ml, (uint32_t) r->fuam.arr[0] << 24);
    set_mask(&r->dcm, ml, (uint32_t) r->dcm.arr[0] << 24);
    for (i = 0; i < r->standard.nCount; ++i)
        set_mask(&r->standard.arr[i].sm, ml, (uint32_t) r->standard.arr[i].sm.arr[0] << 24);
}
static void hdr_rreq_ml2(Jp2Family *f, int box) { hdr_rreq_mask_length(f, box, 2); }
static void hdr_rreq_ml3(Jp2Family *f, int box) { hdr_rreq_mask_length(f, box, 3); }
static void hdr_jpx_jplh_cdef_pairs(Jp2Family *f, int box) {
    hdr_jpx_jplh(f, box);
    set_cdef(append_child(&f->boxes.arr[f->boxes.nCount - 1].payload.u.jplh), 2, 0, 0, 1);
}

/* Appended entries only: array positions are in fixture names. */
static const RuleMutant header_mutants[] = {
    { "jp2h.palette", hdr_palette, "pclr (2 entries) and cmap through it: valid", CF_JP2, 0, X_VALID },
    { "jp2h.resolution", hdr_resolution, "res with resc and resd: valid", CF_JP2, 0, X_VALID },
    { "jp2h.other-box", hdr_other_child, "xml box in jp2h after colr: valid and skipped (T.800 I.5.3)", CF_JP2, 0, X_VALID },
    { "jp2h.second-colr", hdr_second_colr, "second enumerated colr (greyscale): valid, readers use the first", CF_JP2, 0, X_VALID },
    { "colr.prec-approx", hdr_colr_prec_approx, "PREC -1 and APPROX 1: writer fields readers ignore, valid", CF_JP2, 0, X_VALID },
    { "cdef.pairs", hdr_cdef_unspecified, "grey and two unspecified channels from a palette: the pair (65535, 65535) may repeat, valid", CF_JP2, 0, X_VALID },
    { "res.resc-resd", hdr_res_other, "resd and an unknown box in res: valid", CF_JP2, 0, X_VALID },
    { "jp2.one-jp2h", hdr_no_jp2h, "no jp2h (T.800 I.5.3)", CF_JP2, 0, X_STD },
    { "jp2.one-jp2h", hdr_two_jp2h, "two jp2h boxes", CF_JP2, 0, X_STD },
    { "jp2h.position", hdr_jp2h_after_jp2c, "jp2h after jp2c (T.800 I.2)", CF_JP2, 0, X_STD },
    { "jp2.codestream", hdr_no_jp2c, "no jp2c (T.800 I.2)", CF_JP2, 0, X_STD },
    { "jp2h.ihdr-first", hdr_colr_first, "colr before ihdr", CF_JP2, 0, X_STD },
    { "jp2h.colr", hdr_no_colr, "no colr", CF_JP2, 0, X_STD },
    { "jp2h.colr-contiguous", hdr_colr_split, "colr, xml, colr", CF_JP2, 0, X_STD },
    { "ihdr.bpcc", hdr_bpcc_not_varying, "bpcc with BPC 7 (I.5.3.2: only when BPC is 255)", CF_JP2, 0, X_STD },
    { "ihdr.bpcc", hdr_bpc_255_no_bpcc, "BPC 255 without bpcc", CF_JP2, 0, X_STD },
    { "header.one-bpcc", hdr_two_bpcc, "two bpcc", CF_JP2, 0, X_STD },
    { "bpcc.count", hdr_bpcc_count, "BPC 255, bpcc of 2 entries for NC 1 (and BPC 255 for equal depths)", CF_JP2, 0, X_STD },
    { "bpcc.depth", hdr_bpcc_depth, "BPC 255, bpcc entry 8 for Ssiz 7 (and BPC 255 for equal depths)", CF_JP2, 0, X_STD },
    { "ihdr.height", hdr_ihdr_height, "HEIGHT = Ysiz - YOsiz + 1", CF_JP2, 0, X_STD },
    { "ihdr.width", hdr_ihdr_width, "WIDTH = Xsiz - XOsiz + 1", CF_JP2, 0, X_STD },
    { "ihdr.nc", hdr_ihdr_nc, "NC 2 for Csiz 1", CF_JP2, 0, X_STD },
    { "ihdr.bpc", hdr_ihdr_bpc, "BPC 8 for Ssiz 7", CF_JP2, 0, X_STD },
    { "ihdr.bpc", hdr_ihdr_signed, "BPC signed for unsigned Ssiz", CF_JP2, 0, X_STD },
    { "ihdr.c", hdr_ihdr_c, "C 0 (uncompressed, T.801 only)", CF_JP2, 0, X_STD },
    { "ihdr.unkc", hdr_ihdr_unkc, "UnkC 2 (reserved)", CF_JP2, 0, X_STD },
    { "ihdr.nc", hdr_ihdr_nc_zero, "NC 0", CF_JP2, 0, X_STD },
    { "ihdr.bpc", hdr_ihdr_bpc_38, "BPC 38 (reserved)", CF_JP2, 0, X_STD },
    { "colr.method", hdr_colr_method, "METH 3 (JP2: 1 or 2)", CF_JP2, 0, X_STD },
    { "colr.enumcs", hdr_colr_enumcs, "first colr EnumCS 12 (CMYK; JP2: 16, 17 or 18)", CF_JP2, 0, X_STD },
    { "colr.enumcs-length", hdr_colr_enumcs_length, "METH 1 with a byte after EnumCS", CF_JP2, 0, X_STD },
    { "header.pclr-cmap", hdr_pclr_no_cmap, "pclr without cmap", CF_JP2, 0, X_STD },
    { "header.pclr-cmap", hdr_cmap_no_pclr, "cmap without pclr", CF_JP2, 0, X_STD },
    { "header.one-pclr", hdr_two_pclr, "two pclr", CF_JP2, 0, X_STD },
    { "header.one-cmap", hdr_two_cmap, "two cmap", CF_JP2, 0, X_STD },
    { "pclr.entries-length", hdr_pclr_entries, "NE 2 x 1 byte, 1 byte of entries", CF_JP2, 0, X_STD },
    { "pclr.ne", hdr_pclr_ne_zero, "NE 0", CF_JP2, 0, X_STD },
    { "cmap.pcol-zero", hdr_cmap_pcol_zero, "MTYP 0 with PCOL 1", CF_JP2, 0, X_STD },
    { "cmap.component", hdr_cmap_component, "CMP 1 for NC 1", CF_JP2, 0, X_STD },
    { "cmap.palette-column", hdr_cmap_palette_column, "PCOL 1 for NPC 1", CF_JP2, 0, X_STD },
    { "cmap.mtyp", hdr_cmap_mtyp, "MTYP 2 (reserved)", CF_JP2, 0, X_STD },
    { "cdef.pairs", hdr_cdef_pairs, "two descriptions Typ 0, Asoc 1", CF_JP2, 0, X_STD },
    { "cdef.channel", hdr_cdef_channel, "Cn 1 for one channel", CF_JP2, 0, X_STD },
    { "cdef.typ", hdr_cdef_typ, "Typ 3 (reserved)", CF_JP2, 0, X_STD },
    { "header.one-cdef", hdr_two_cdef, "two cdef", CF_JP2, 0, X_STD },
    { "header.one-res", hdr_two_res, "two res", CF_JP2, 0, X_STD },
    { "res.resc-resd", hdr_res_two_resc, "two resc in res", CF_JP2, 0, X_STD },
    { "res.resc-resd", hdr_res_empty, "res with neither resc nor resd", CF_JP2, 0, X_STD },
    { "res.vd", hdr_res_zero, "resc VRcD 0", CF_JP2, 0, X_STD },
    { "cdef.opacity", hdr_cdef_opacity, "grey and opacity channels from a palette: valid", CF_JP2, 0, X_VALID },
    { "jpx.header-defaults", hdr_jpx_defaults, "jp2h ihdr and colr as the default for both codestreams, empty jpch: valid", CF_JPX, 0, X_VALID },
    { "jpch.palette", hdr_jpx_palette, "pclr and cmap in a jpch: valid", CF_JPX, 0, X_VALID },
    { "jpx.no-jp2h", hdr_jpx_no_jp2h, "no jp2h, an ihdr in each jpch (T.801: jp2h optional)", CF_JPX, 0, X_VALID },
    { "jplh.res", hdr_jpx_jplh, "jplh with res: valid", CF_JPX, 0, X_VALID },
    { "jpch.ihdr", hdr_jpx_no_ihdr, "no jp2h, first jpch empty (T.801 M.11.6)", CF_JPX, 0, X_STD },
    { "jp2h.position", hdr_jpx_late_jp2h, "jp2h after the codestreams (T.801 M.11.5)", CF_JPX, 0, X_STD },
    { "header.one-ihdr", hdr_jpx_two_ihdr, "two ihdr in a jpch", CF_JPX, 0, X_STD },
    { "ihdr.width", hdr_jpx_ihdr_width, "jpch ihdr WIDTH = Xsiz - XOsiz + 1", CF_JPX, 0, X_STD },
    { "header.pclr-cmap", hdr_jpx_pclr_no_cmap, "pclr in a jpch without cmap", CF_JPX, 0, X_STD },
    { "colr.one-method", hdr_jpx_two_enumerated, "two enumerated colr in jp2h (T.801 M.11.7.1)", CF_JPX, 0, X_STD },
    { "cdef.pairs", hdr_jpx_jplh_cdef_pairs, "jplh cdef with two descriptions Typ 0, Asoc 1", CF_JPX, 0, X_STD },
    { "rreq.mask-length", hdr_rreq_ml2, "rreq masks of 2 bytes (ML 2): valid", CF_JPX, 0, X_VALID },
    { "rreq.ml", hdr_rreq_ml3, "rreq masks of 3 bytes (ML 3; T.801 Table M.15: 1, 2, 4 or 8)", CF_JPX, 0, X_STD },
    { "ihdr.extent", hdr_ihdr_extent, "a byte after the ihdr fields: a 23-byte box (I.5.3.1: 22)", CF_JP2, 0, X_STD },
    { "cdef.extent", hdr_cdef_extent, "a byte after the cdef descriptions", CF_JP2, 0, X_STD },
    { "res.extent", hdr_res_extent, "a byte after the resc fields", CF_JP2, 0, X_STD },
};

/* ------------------------------------------------------------------------ */
/* 5c. Length and region mutants (byte level)                                */
/* ------------------------------------------------------------------------ */

/* Signature box contents corrupted (LBox/TBox intact): the only invalid
 * preamble the code mutants cannot produce. */
static void emit_signature_mutant(Bytes valid, cf_kind kind, const char *base_name) {
    Bytes m = bytes_dup(valid);
    char name[256];
    if (valid.len < 12) return;
    put32(m.data + 8, 0);
    snprintf(name, sizeof name, "%s-sig-bad", base_name);
    emit(m, kind, name, "jP.contents", "signature contents zeroed (T.800 I.5.1)", NULL, X_STD);
    free(m.data);
}

static void emit_length_mutants(Bytes valid, cf_kind kind, const char *base_name) {
    Regions rs = walk(&valid);
    int i;
    for (i = 0; i < rs.n; ++i) {
        const Region *r = &rs.r[i];
        uint32_t v = r->len_width == 2 ? be16(valid.data + r->len_off) : be32(valid.data + r->len_off);
        uint32_t cand[4];
        int n = 0, k;
        cand[n++] = v - 1;
        cand[n++] = v + 1;
        if (strcmp(r->kind, "Psot") == 0) cand[n++] = 13;
        if (strcmp(r->kind, "LBox") == 0) cand[n++] = 7;
        if (strcmp(r->kind, "Lxxx") == 0) cand[n++] = 1;
        for (k = 0; k < n; ++k) {
            Bytes m;
            char name[256], field[64];
            int previous;
            for (previous = 0; previous < k; ++previous)
                if (cand[previous] == cand[k]) break;
            if (previous < k) continue;
            m = bytes_dup(valid);
            if (r->len_width == 2) put16(m.data + r->len_off, (uint16_t) cand[k]);
            else put32(m.data + r->len_off, cand[k]);
            snprintf(field, sizeof field, "%s@0x%zx", r->kind, r->len_off);
            snprintf(name, sizeof name, "%s-len-%zx-%u", base_name, r->len_off, cand[k]);
            emit(m, kind, name, field, "length patched", NULL, X_STD);
            free(m.data);
        }
    }
    free(rs.r);
}

/* Patch marker codes in place: the body stays well-formed and only its
 * selector changes. Unknown marker codes are standard-invalid and
 * profile-valid (the server skips them); SIZ/SOT in the wrong place are
 * invalid at both layers. Box-type rules use structured mutants so changing
 * the type does not accidentally remove a different mandatory box. */
static void emit_code_mutants(Bytes valid, cf_kind kind, const char *base_name) {
    Regions rs = walk(&valid);
    int i;
    for (i = 0; i < rs.n; ++i) {
        const Region *r = &rs.r[i];
        static const struct { const char *kind; uint32_t value; const char *note; } patches[] = {
            { "Lxxx", 65392u,      "code FF70: undefined marker" },
            { "Lxxx", 65361u,      "code FF51: SIZ out of place" },
            { "Lxxx", 65424u,      "code FF90: SOT where a segment was" },
        };
        size_t k;
        for (k = 0; k < sizeof patches / sizeof *patches; ++k) {
            Bytes m;
            char name[256], field[64];
            if (strcmp(r->kind, patches[k].kind) != 0) continue;
            if (r->len_width == 2 && be16(valid.data + r->start) == patches[k].value)
                continue;
            m = bytes_dup(valid);
            if (r->len_width == 2) put16(m.data + r->start, (uint16_t) patches[k].value);
            else put32(m.data + r->start + 4, patches[k].value);
            snprintf(field, sizeof field, "%s@0x%zx", r->kind, r->start);
            snprintf(name, sizeof name, "%s-code-%zx-%u", base_name, r->start, patches[k].value);
            emit(m, kind, name, field, patches[k].note, NULL, X_STD);
            free(m.data);
        }
    }
    free(rs.r);
}

/* Inserts n bytes at `at` and grows every length whose region encloses it
 * (LBox, Psot, Lxxx), so only the inserted bytes are new. */
static Bytes insert_bytes(Bytes valid, size_t at, const void *bytes, size_t n) {
    Regions rs = walk(&valid);
    Bytes m = { xcalloc(valid.len + n + 1), valid.len + n };
    int i;
    memcpy(m.data, valid.data, at);
    memcpy(m.data + at, bytes, n);
    memcpy(m.data + at + n, valid.data + at, valid.len - at);
    for (i = 0; i < rs.n; ++i) {
        const Region *r = &rs.r[i];
        if (!(r->start < at && at < r->end)) continue;
        if (r->len_width == 2) put16(m.data + r->len_off, (uint16_t) (be16(valid.data + r->len_off) + n));
        else put32(m.data + r->len_off, (uint32_t) (be32(valid.data + r->len_off) + n));
    }
    free(rs.r);
    return m;
}

/* Insertions the structured mutants cannot make: codes the model has no
 * alternative for, placed before the first SOT, and an Iplt longer than
 * the model's ten bytes. Invalid at layer 1; the server skipped the main-
 * header codes by length and read any Iplt length, which the profile now
 * rejects. */
static void emit_insertion_mutants(Bytes valid, cf_kind kind, const char *base_name) {
    static const struct { const char *name; const char *bytes; size_t n; const char *note; } main_codes[] = {
        { "ppt", "\xFF\x61\x00\x03\x00", 5, "PPT, a tile-part header marker, in the main header" },
        { "sop", "\xFF\x91\x00\x04\x00\x00", 6, "SOP, a packet marker, in the main header" },
        { "ff30-length", "\xFF\x30\x00\x02", 4, "FF30 read with a length: FF30 to FF3F have no segment (A.1.3)" },
        { "ff00", "\xFF\x00\x00\x02", 4, "FF00, not a marker, in the main header" },
    };
    static const unsigned char continuation[10] = {
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
    Regions rs = walk(&valid);
    size_t sot = 0, plt = 0, k;
    int i, tp = -1;
    for (i = 0; i < rs.n && tp < 0; ++i)
        if (strcmp(rs.r[i].kind, "Psot") == 0) { tp = i; sot = rs.r[i].start; }
    for (i = tp + 1; i < rs.n && plt == 0; ++i)
        if (rs.r[i].parent == tp && be16(valid.data + rs.r[i].start) == HV_PLT)
            plt = rs.r[i].payload_start + 1;              /* first Iplt, after Zplt */
    free(rs.r);
    if (tp < 0 || plt == 0) die("insertion mutants: no tile-part with PLT");
    for (k = 0; k < sizeof main_codes / sizeof *main_codes; ++k) {
        Bytes m = insert_bytes(valid, sot, main_codes[k].bytes, main_codes[k].n);
        char name[256];
        snprintf(name, sizeof name, "%s-main-%s", base_name, main_codes[k].name);
        emit(m, kind, name, "main.marker-code", main_codes[k].note, NULL, X_STD);
        free(m.data);
    }
    {
        Bytes m = insert_bytes(valid, plt, continuation, sizeof continuation);
        char name[256];
        snprintf(name, sizeof name, "%s-plt-iplt-eleven-bytes", base_name);
        emit(m, kind, name, "plt.iplt-length",
             "11-byte Iplt: beyond the model's ten bytes; T.800 sets no bound", NULL, X_STD);
        free(m.data);
    }
}

/* Reader Requirements counts and extent the typed mutants cannot break
 * (NSF, NVF and ML are determinants): NSF and NVF one too high, and a byte
 * after the contents. Invalid at layer 1; the profile keeps rreq opaque. */
static void emit_rreq_mutants(Bytes valid, const char *base_name) {
    size_t rreq = 12 + be32(valid.data + 12), payload = rreq + 8, nsf, nvf;
    unsigned ml;
    char name[256];
    Bytes m;
    if (be32(valid.data + rreq + 4) != HV_BOX_RREQ) die("rreq mutants: rreq is not the third box");
    ml = valid.data[payload];
    nsf = payload + 1 + 2 * ml;
    nvf = nsf + 2 + (2 + ml) * be16(valid.data + nsf);
    m = bytes_dup(valid);
    put16(m.data + nsf, (uint16_t) (be16(m.data + nsf) + 1));
    snprintf(name, sizeof name, "%s-header-rreq.nsf", base_name);
    emit(m, CF_JPX, name, "rreq.nsf", "NSF one more than the standard features", NULL, X_STD);
    free(m.data);
    m = bytes_dup(valid);
    put16(m.data + nvf, (uint16_t) (be16(m.data + nvf) + 1));
    snprintf(name, sizeof name, "%s-header-rreq.nvf", base_name);
    emit(m, CF_JPX, name, "rreq.nvf", "NVF 1 with no vendor feature", NULL, X_STD);
    free(m.data);
    m = insert_bytes(valid, nvf + 2, "\x00", 1);                 /* at the end of the box */
    put32(m.data + rreq, be32(m.data + rreq) + 1);
    snprintf(name, sizeof name, "%s-header-rreq.extent", base_name);
    emit(m, CF_JPX, name, "rreq.extent", "a byte after the vendor features, inside the box", NULL, X_STD);
    free(m.data);
}

/* ------------------------------------------------------------------------ */
/* Driver                                                                    */
/* ------------------------------------------------------------------------ */

static void run_base(Base base, const char *companions) {
    Bytes valid = encode_family(base.file, 1);
    char name[256];
    size_t i;
    Jp2Family *work = xcalloc(sizeof *work);

    emit(valid, base.kind, base.name, NULL, "base", companions, X_VALID);
    emit_signature_mutant(valid, base.kind, base.name);
    emit_length_mutants(valid, base.kind, base.name);
    emit_code_mutants(valid, base.kind, base.name);
    if (base.jp2c_box >= 0)
        emit_insertion_mutants(valid, base.kind, base.name);
    if (base.kind == CF_JPX && base.jp2c_box >= 0)
        emit_rreq_mutants(valid, base.name);

    if (base.jp2c_box >= 0) {
        int has_precincts = cod_of(base.file, base.jp2c_box)->scod.customPrecincts;
        for (i = 0; i < sizeof field_mutants / sizeof *field_mutants; ++i) {
            const FieldMutant *fm = &field_mutants[i];
            Bytes m;
            if (fm->needs_precincts != has_precincts) continue;
            *work = *base.file;
            fm->set(work, base.jp2c_box, fm->value);
            sync_headers(work);
            m = encode_family(work, 0);
            snprintf(name, sizeof name, "%s-%s-%lld", base.name, fm->field, (long long) fm->value);
            emit(m, base.kind, name, fm->field, fm->note, companions, fm->expect);
            free(m.data);
        }
        for (i = 0; i < sizeof rule_mutants / sizeof *rule_mutants; ++i) {
            const RuleMutant *rm = &rule_mutants[i];
            Bytes m;
            if (rm->only_kind && rm->only_kind != (int) base.kind) continue;
            if (rm->needs_precincts != has_precincts) continue;
            *work = *base.file;
            rm->apply(work, base.jp2c_box);
            sync_headers(work);
            m = encode_family(work, 0);
            snprintf(name, sizeof name, "%s-rule-%s-%zu", base.name, rm->name, i);
            emit(m, base.kind, name, rm->name, rm->note, companions, rm->expect);
            free(m.data);
        }
        for (i = 0; i < sizeof header_mutants / sizeof *header_mutants; ++i) {
            const RuleMutant *rm = &header_mutants[i];
            Bytes m;
            if (rm->only_kind != (int) base.kind || has_precincts) continue;
            *work = *base.file;
            rm->apply(work, base.jp2c_box);
            m = encode_family(work, 0);
            snprintf(name, sizeof name, "%s-header-%s-%zu", base.name, rm->name, i);
            emit(m, base.kind, name, rm->name, rm->note, companions, rm->expect);
            free(m.data);
        }
    } else {
        for (i = 0; i < sizeof linked_rule_mutants / sizeof *linked_rule_mutants; ++i) {
            const RuleMutant *rm = &linked_rule_mutants[i];
            Bytes m;
            *work = *base.file;
            rm->apply(work, -1);
            m = encode_family(work, 0);
            snprintf(name, sizeof name, "%s-rule-%s-%zu", base.name, rm->name, i);
            emit(m, base.kind, name, rm->name, rm->note, companions, rm->expect);
            free(m.data);
        }
    }
    free(work);
    free(valid.data);
    free(base.file);
}

/* The encoder uses `abcd` as the canonical other type. Patch its TBox on
 * the wire to exercise the decoder's full unknown-type fallback. */
static void patch_other_type(Bytes *b, int nested, uint32_t previous, uint32_t type) {
    Regions rs = walk(b);
    int i, found = 0;
    for (i = 0; i < rs.n; ++i) {
        const Region *r = &rs.r[i];
        if (strcmp(r->kind, "LBox") != 0 || (r->parent >= 0) != nested ||
            be32(b->data + r->start + 4) != previous)
            continue;
        put32(b->data + r->start + 4, type);
        found++;
    }
    free(rs.r);
    if (found != 1) die("expected one target unknown box");
}

static void emit_unknown_box_types(void) {
    Base base = base_jp2(0, 0);
    Bytes b;
    Superbox *superbox;
    InnerBox *child;

    rule_unknown_box(base.file, -1);
    b = encode_family(base.file, 1);
    patch_other_type(&b, 0, 1633837924u, be32((const unsigned char *) "wxyz"));
    emit(b, CF_JP2, "jp2-unknown-top-wxyz", "file.unknown-box",
         "unlisted top-level box type: valid and skipped", NULL, X_VALID);
    patch_other_type(&b, 0, be32((const unsigned char *) "wxyz"), 0xF0E1D2C3u);
    emit(b, CF_JP2, "jp2-unknown-top-binary", "file.unknown-box",
         "unlisted non-ASCII box type: valid and skipped", NULL, X_VALID);
    free(b.data);
    free(base.file);

    base = base_jpx_embedded();
    rule_unknown_box(base.file, -1);
    b = encode_family(base.file, 1);
    patch_other_type(&b, 0, 1633837924u, be32((const unsigned char *) "wxyz"));
    emit(b, CF_JPX, "jpx-unknown-top-wxyz", "file.unknown-box",
         "unlisted top-level box type: valid and skipped", NULL, X_VALID);
    free(b.data);
    free(base.file);

    base = base_jpx_embedded();
    superbox = &base.file->boxes.arr[4].payload.u.jpch;
    child = &superbox->children.arr[superbox->children.nCount++];
    child->payload.kind = InnerPayload_other_PRESENT;
    OCTETS(child->payload.u.other.data, "\x01", 1);
    b = encode_family(base.file, 1);
    patch_other_type(&b, 1, 1633837924u, be32((const unsigned char *) "wxyz"));
    emit(b, CF_JPX, "jpx-unknown-inner-wxyz", "file.unknown-box",
         "unlisted box inside jpch: valid and skipped", NULL, X_VALID);
    free(b.data);
    free(base.file);
}

static void emit_associations(void) {
    Base base = base_jpx_embedded();
    Bytes b = encode_family(base.file, 1), mutant;
    size_t offset = b.len;
    TopBox *box = &base.file->boxes.arr[base.file->boxes.nCount++];
    InnerBox *child;
    free(b.data);
    box->payload.kind = TopPayload_asoc_PRESENT;
    box->payload.u.asoc.children.nCount = 2;
    child = &box->payload.u.asoc.children.arr[0];
    child->payload.kind = InnerPayload_lbl_PRESENT;
    OCTETS(child->payload.u.lbl.data, "test", 5); /* includes terminating NUL */
    child = &box->payload.u.asoc.children.arr[1];
    child->payload.kind = InnerPayload_xml_PRESENT;
    OCTETS(child->payload.u.xml.data, "<meta/>", 7);
    b = encode_family(base.file, 1);
    emit(b, CF_JPX, "jpx-asoc", "asoc.opaque",
         "T.801 M.11.11: label associated with XML; profile preserves opaque contents", NULL, X_VALID);

    mutant = bytes_dup(b);
    put32(mutant.data + offset + 8, 7);
    emit(mutant, CF_JPX, "jpx-asoc-child-length", "asoc.opaque",
         "inner LBox below 8: standard invalid, profile preserves opaque contents", NULL, X_STD);
    free(mutant.data);
    mutant = bytes_dup(b);
    put32(mutant.data + offset, (uint32_t) (b.len - offset + 1));
    emit(mutant, CF_JPX, "jpx-asoc-outer-length", "asoc.outer-length",
         "outer LBox exceeds file: invalid at both layers", NULL, X_STD);
    free(mutant.data);
    free(b.data);

    box->payload.u.asoc.children.nCount = 1;
    b = encode_family(base.file, 0);
    emit(b, CF_JPX, "jpx-asoc-one-child", "asoc.opaque",
         "T.801 M.11.11 requires at least two children; profile preserves opaque contents", NULL, X_STD);
    free(b.data);
    free(base.file);
}

int main(int argc, char **argv) {
    char path[1024];
    if (argc != 2) {
        fprintf(stderr, "usage: vectors <output-directory>\n");
        return EXIT_FAILURE;
    }
    out_dir = argv[1];
    snprintf(path, sizeof path, "%s/manifest.tsv", out_dir);
    manifest = fopen(path, "w");
    if (!manifest) { perror(path); return EXIT_FAILURE; }
    fprintf(manifest, "file\tkind\tstandard\tprofile\treason\tfield\tnote\tcompanions\n");

    encode_buffer = xcalloc(FILE_BUFFER_BYTES);
    dec_family = xcalloc(sizeof *dec_family);
    dec_jp2 = xcalloc(sizeof *dec_jp2);
    dec_jpx = xcalloc(sizeof *dec_jpx);

    /* Sanity: the box-type constants in the model are the four-cc values. */
    if (be32((const unsigned char *) "jp2c") != HV_BOX_JP2C || be32((const unsigned char *) "flst") != HV_BOX_FLST)
        die("box type constants do not match four-cc values");

    run_base(base_jp2(0, 0), NULL);
    run_base(base_jp2(1, 1), NULL);
    run_base(base_jpx_embedded(), NULL);
    emit_unknown_box_types();
    emit_associations();

    /* Linked JPX: emit the two companion frames first, read back their
     * codestream extents, then build the JPX that references them. The
     * server resolves a relative "file://" path against the directory of
     * the JPX itself, so the test copies the companions next to the vector. */
    {
        static const char *const urls[2] = { "file://./jpx-linked-frame1.jp2", "file://./jpx-linked-frame2.jp2" };
        uint64_t off[2];
        uint32_t len[2];
        int i;
        for (i = 0; i < 2; ++i) {
            Base frame = base_jp2(0, 0);
            Bytes b = encode_family(frame.file, 1);
            Regions rs = walk(&b);
            char fname[64];
            snprintf(fname, sizeof fname, "jpx-linked-frame%d.jp2", i + 1);
            write_file(out_dir, fname, b);
            off[i] = rs.soc_off;
            len[i] = (uint32_t) (rs.eoc_end - rs.soc_off);
            companions[companion_count].url = urls[i];
            companions[companion_count].offset = off[i];
            companions[companion_count++].length = len[i];
            free(rs.r);
            free(b.data);
            free(frame.file);
        }
        run_base(base_jpx_linked(urls, off, len, 2),
                 "jpx-linked-frame1.jp2,jpx-linked-frame2.jp2");

        /* T.801 M.11.2 / M.11.3.1: fragment DR indexes the reference table,
         * independently of codestream order. Use distinguishable sources
         * to expose swapped references and per-codestream state mixups. */
        {
            static const char *const graph_urls[2] = {
                "file://./jpx-linked-frame1.jp2", "file://./jpx-graph-frame2.jp2"
            };
            static const char *const names[3] = {
                "jpx-graph-sequential", "jpx-graph-reversed", "jpx-graph-repeated"
            };
            static const int references[3][2] = {{1, 2}, {2, 1}, {2, 2}};
            Base frame = base_jp2(0, 0);
            Bytes b;
            Regions rs;
            int graph, stream;
            rule_plt_boundaries(frame.file, frame.jp2c_box);
            b = encode_family(frame.file, 1);
            rs = walk(&b);
            write_file(out_dir, "jpx-graph-frame2.jp2", b);
            off[1] = rs.soc_off;
            len[1] = (uint32_t) (rs.eoc_end - rs.soc_off);
            companions[companion_count].url = graph_urls[1];
            companions[companion_count].offset = off[1];
            companions[companion_count++].length = len[1];
            free(rs.r);
            free(b.data);
            free(frame.file);
            for (graph = 0; graph < 3; ++graph) {
                Base linked = base_jpx_linked(graph_urls, off, len, 2);
                for (stream = 0; stream < 2; ++stream) {
                    Fragment *fragment = &linked.file->boxes.arr[6 + stream].payload.u.ftbl.children.arr[0].payload.u.flst.fragments.arr[0];
                    int reference = references[graph][stream];
                    fragment->dr = reference;
                    fragment->off = off[reference - 1];
                    fragment->len = len[reference - 1];
                }
                b = encode_family(linked.file, 1);
                emit(b, CF_JPX, names[graph], "jpx.reference-order",
                     "T.801 M.11.2/M.11.3.1: distinct companions, DR mapping independent of codestream order",
                     "jpx-linked-frame1.jp2,jpx-graph-frame2.jp2", X_VALID);
                free(b.data);
                free(linked.file);
            }
        }
    }

    fclose(manifest);
    fprintf(stderr, "vectors: %d vectors written to %s (%d valid at both layers)\n",
            emitted, out_dir, emitted_valid);
    if (mismatches) {
        fprintf(stderr, "vectors: %d mutant(s) did not produce the expected label; "
                        "fix the mutant, its expectation, or the model\n", mismatches);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
