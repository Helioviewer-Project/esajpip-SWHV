/* vectors.c — builds the JPEG 2000 test-vector corpus from the ACN model.
 *
 * Usage:  vectors <output-directory>
 *
 * Produces complete .jp2 / .jpx files plus manifest.tsv. Every vector is
 * labeled at both layers by decoding it with the generated decoders, which
 * also check the ASN.1 constraints, and applying crossfield.c. Labels are
 * evaluated mechanically; the model, rules and mutant expectations (the
 * label and the reason each vector is meant to get) are human-authored and
 * checked against the cited standards.
 *
 * Structure:
 *   1. Generated-API adaptation (the only place that names generated symbols
 *      beyond field access).
 *   2. Byte-level walker: finds every Lxxx / Psot / LBox in an encoded file
 *      and the codestream extent inside a jp2c, following the superboxes
 *      each layer reads (children_offset).
 *   3. Base builders: canonical profile-valid JP2, embedded JPX, linked JPX.
 *   4. Emission, with the labels of label.c.
 *   5. Mutation catalogues: field, rule, header box, and byte-level
 *      (signature, length, code, insertion, rreq).
 *   Driver: the bases and their mutants, unknown box types, boxes nested
 *      where they do not belong, associations, and the linked-JPX reference
 *      graphs.
 *
 * Build: spec/check-model.sh compiles this file with label.c, crossfield.c,
 * mapping.c, ../../lib/hv_rules.c (with lib/ on the include path) and the
 * generated C of the four modules and the asn1scc runtime, then runs it;
 * its command is the reference.
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
#include "label.h"
#include "mapping.h"

/* ------------------------------------------------------------------------ */
/* 1. Generated-API adaptation                                              */
/* ------------------------------------------------------------------------ */

/* asn1scc 4.x C conventions. If a generated name differs, change it here. */
#define ENC(T)   T##_ACN_Encode

/* Encoded size upper bound for the top-level file types. asn1scc emits
 * <Type>_REQUIRED_BYTES_FOR_ACN_ENCODING; it is an upper bound over the
 * corpus SIZE constraints, which is exactly what we want for the buffer. */
#ifndef FILE_BUFFER_BYTES
#define FILE_BUFFER_BYTES Jp2Family_REQUIRED_BYTES_FOR_ACN_ENCODING
#endif


/* Siz-Profile's bound on Xsiz and Ysiz (j2k-headers.asn1): 2,147,483,647.
 * ../check-model.sh checks this copy against the model. */
#define PROFILE_MAX_DIMENSION 2147483647u

/* A marker code that T.800 does not define (Table A.2). */
enum { UNDEFINED_MARKER = 0xFF70 };

/* ------------------------------------------------------------------------ */
/* Small utilities                                                          */
/* ------------------------------------------------------------------------ */

typedef struct {
    unsigned char *data;
    size_t len;
} Bytes;

/* The elements a generated array (SEQUENCE OF, OCTET STRING) holds: its
 * SIZE bound. */
#define CAPACITY(a) ((int) (sizeof (a) / sizeof (a)[0]))

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

static BoxLayer profile_layer(cf_kind kind) {
    return kind == CF_JP2 ? LAYER_PROFILE_JP2 : LAYER_PROFILE_JPX;
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

/* The boxes layer 1 reads (children_offset), and the codestream of each
 * top-level jp2c. */
static int walk_boxes(Regions *rs, const Bytes *b, size_t pos, size_t end, int parent, int depth) {
    while (pos < end) {
        uint32_t l, t;
        size_t box_end;
        int idx, offset;
        if (end - pos < HV_BOX_HEADER) return 0;
        l = be32(b->data + pos);
        t = be32(b->data + pos + 4);
        if (l < HV_BOX_HEADER) return 0;         /* XLBox and L = 0 are not in the corpus */
        box_end = pos + l;
        if (box_end > end) return 0;
        idx = region_add(rs, (Region) { "LBox", pos, box_end, pos, 4, pos + HV_BOX_HEADER,
                                        parent });
        if (depth == 0 && t == HV_BOX_JP2C) {
            if (!walk_codestream(rs, b, pos + HV_BOX_HEADER, box_end, idx)) return 0;
        } else if ((offset = children_offset(t, depth, LAYER_STANDARD)) >= 0) {
            size_t child_start = pos + HV_BOX_HEADER + (size_t) offset;
            if (child_start > box_end ||
                !walk_boxes(rs, b, child_start, box_end, idx, depth + 1))
                return 0;
        }
        pos = box_end;
    }
    return 1;
}

static Regions walk(const Bytes *b) {
    Regions rs = { NULL, 0, 0, 0, 0 };
    if (!walk_boxes(&rs, b, 0, b->len, -1, 0))
        die("walker: generated file is not structurally sound");
    return rs;
}

/* The first top-level box of a type in an encoded file (die if none). */
static const Region *find_top_box(const Regions *rs, const Bytes *b, uint32_t type) {
    int i;
    for (i = 0; i < rs->n; ++i)
        if (rs->r[i].parent < 0 && strcmp(rs->r[i].kind, "LBox") == 0 &&
            be32(b->data + rs->r[i].start + 4) == type)
            return &rs->r[i];
    die("walker: no top-level box of the requested type");
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* 3. Base builders (layer-1 structs, profile-valid)                        */
/* ------------------------------------------------------------------------ */

#define OCTETS(field, src, n) \
    do { memcpy((field).arr, (src), (n)); (field).nCount = (int) (n); } while (0)

/* Top-level boxes by type: `kind` is a TopPayload_<box>_PRESENT value. */
static int box_index(const Jp2Family *f, int kind, int nth) {
    int i;
    for (i = 0; i < f->boxes.nCount; ++i)
        if ((int) f->boxes.arr[i].payload.kind == kind && nth-- == 0) return i;
    die("no box of the requested type in the file");
    return -1;
}

static void remove_box(Jp2Family *f, int at) {
    int i;
    for (i = at + 1; i < f->boxes.nCount; ++i) f->boxes.arr[i - 1] = f->boxes.arr[i];
    f->boxes.nCount--;
}

/* Moves box `from` to position `to`, the boxes between moving up or down
 * by one. */
static void move_box(Jp2Family *f, int from, int to) {
    TopBox *box = xcalloc(sizeof *box);
    int i;
    *box = f->boxes.arr[from];
    for (i = from; i > to; --i) f->boxes.arr[i] = f->boxes.arr[i - 1];
    for (i = from; i < to; ++i) f->boxes.arr[i] = f->boxes.arr[i + 1];
    f->boxes.arr[to] = *box;
    free(box);
}

/* Inserts a copy of box `from` at position `at`. */
static void copy_box(Jp2Family *f, int from, int at) {
    if (f->boxes.nCount >= CAPACITY(f->boxes.arr))
        die("copy_box: more boxes than Jp2Family.boxes holds");
    f->boxes.arr[f->boxes.nCount] = f->boxes.arr[from];
    f->boxes.nCount++;
    move_box(f, f->boxes.nCount - 1, at);
}

static void build_siz(Siz *s, int width, int height) {
    s->fixed.rsiz = 0;
    s->fixed.xsiz = width;  s->fixed.ysiz = height;
    s->fixed.xosiz = 0;     s->fixed.yosiz = 0;
    s->fixed.xtsiz = width; s->fixed.ytsiz = height;
    s->fixed.xtosiz = 0;    s->fixed.ytosiz = 0;
    s->fixed.csiz = 1;
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

static void add_signature_and_ftyp(Jp2Family *f, Brand brand) {
    TopBox *b;
    Ftyp *ftyp;
    add_opaque_box(f, HV_BOX_JP, "\x0D\x0A\x87\x0A", 4);
    b = &f->boxes.arr[f->boxes.nCount++];
    b->payload.kind = TopPayload_ftyp_PRESENT;
    ftyp = &b->payload.u.ftyp;
    ftyp->header.brand = brand;
    /* MinV: T.800 I.5.2 requires 0 in a JP2 file, T.801 Annex M requires 1
     * in a JPX file (file.ftyp-minor, layer 1). Readers shall parse the file
     * whatever the value, and esajpip ignores it, so the profile does not
     * constrain it. */
    ftyp->header.minor = brand == HV_BRAND_JPX ? 1 : 0;
    ftyp->compat.nCount = 1;
    ftyp->compat.arr[0] = brand;
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

/* A JPX file: the second box, ftyp, has the JPX brand. */
static int is_jpx(const Jp2Family *f) {
    return f->boxes.nCount > 1 && f->boxes.arr[1].payload.kind == TopPayload_ftyp_PRESENT &&
           f->boxes.arr[1].payload.u.ftyp.header.brand == HV_BRAND_JPX;
}

/* A colr of file f, with the APPROX its kind requires of a writer: 0 in a
 * JP2 file (T.800 I.5.3.3), 1 in a JPX file (T.801 M.11.7.2: 1 to 4). */
static void set_colr(const Jp2Family *f, InnerBox *c, int meth, uint32_t enumcs) {
    ColrHeader *h = &c->payload.u.colr.header;
    c->payload.kind = InnerPayload_colr_PRESENT;
    h->meth = meth;
    h->prec = 0;
    h->approx = is_jpx(f) ? 1 : 0;
    h->exist.enumcs = meth == 1;
    h->enumcs = enumcs;
    c->payload.u.colr.rest.nCount = 0;
}

static void add_jp2h(Jp2Family *f, int width, int height) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->payload.kind = TopPayload_jp2h_PRESENT;
    b->payload.u.jp2h.children.nCount = 2;
    set_ihdr(&b->payload.u.jp2h.children.arr[0], width, height);
    set_colr(f, &b->payload.u.jp2h.children.arr[1], 1, 17);          /* greyscale */
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
        c->payload.u.url.header.vers = 0;
        c->payload.u.url.header.flag = 0;
        strncpy((char *) c->payload.u.url.loc, urls[i], sizeof c->payload.u.url.loc - 1);
    }
}

typedef struct {
    const char *name;
    cf_kind kind;
    Jp2Family *file;
    /* The box of the codestream whose fields the field and rule mutants
     * address: the first jp2c (-1 for a linked JPX, which has none). */
    int jp2c_box;
} Base;

static Jp2Family *new_family(void) {
    Jp2Family *f = xcalloc(sizeof *f);
    return f;
}

/* jP ftyp jp2h jp2c */
static Base base_jp2(int levels, int custom_precincts) {
    Base b = { custom_precincts ? "jp2-precincts" : "jp2", CF_JP2, new_family(), -1 };
    add_signature_and_ftyp(b.file, HV_BRAND_JP2);
    add_jp2h(b.file, BASE_W, BASE_H);
    add_jp2c(b.file, BASE_W, BASE_H, levels, custom_precincts, 1);
    b.jp2c_box = box_index(b.file, TopPayload_jp2c_PRESENT, 0);
    return b;
}

/* jP ftyp rreq jp2h jpch jpch jp2c jp2c */
static Base base_jpx_embedded(void) {
    Base b = { "jpx-embedded", CF_JPX, new_family(), -1 };
    add_signature_and_ftyp(b.file, HV_BRAND_JPX);
    add_rreq(b.file, 0);
    add_jp2h(b.file, BASE_W, BASE_H);
    add_jpch(b.file);
    add_jpch(b.file);
    add_jp2c(b.file, BASE_W, BASE_H, 0, 0, 1);
    add_jp2c(b.file, BASE_W, BASE_H, 0, 0, 1);
    b.jp2c_box = box_index(b.file, TopPayload_jp2c_PRESENT, 0);
    return b;
}

/* Linked JPX needs the referenced files' codestream extents; caller
 * supplies them after encoding the companions. jP ftyp rreq jp2h, n jpch,
 * n ftbl, dtbl. */
static Base base_jpx_linked(const char *const *urls, const uint64_t *off, const uint32_t *len,
                            int n) {
    Base b = { "jpx-linked", CF_JPX, new_family(), -1 };
    int i;
    add_signature_and_ftyp(b.file, HV_BRAND_JPX);
    add_rreq(b.file, 1);
    add_jp2h(b.file, BASE_W, BASE_H);
    for (i = 0; i < n; ++i) add_jpch(b.file);
    for (i = 0; i < n; ++i) add_ftbl(b.file, off[i], len[i], i + 1);
    add_dtbl(b.file, urls, n);
    return b;
}

/* ------------------------------------------------------------------------ */
/* 4. Encoding and emission                                                 */
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

/* What a mutant is meant to produce. The decoders decide the label; the
 * expectation only checks that the mutant did what its note says, so a
 * setter that misses its target or a rule that stopped firing is caught at
 * generation time instead of surfacing as a puzzling server-test result.
 * Every vector states its label (Expect) and its reason, the manifest's
 * `reason` column: the first check that failed at the stricter failing
 * layer, a rule name or `decode` where a type rejects the vector before any
 * rule runs (a range, a value set, a box type with no alternative at its
 * level, a region not exactly filled, box_bounds_ok), and NULL for a
 * vector valid at both layers. Where the vector is invalid at both layers
 * and the rule it is named after is a layer-2 rule that a layer-1 rule
 * preempts in the manifest, it also states that layer-2 reason
 * (`profile_reason`, otherwise NULL and unchecked). */
typedef enum {
    X_VALID,   /* valid at both layers */
    X_STD,     /* invalid at both layers */
    X_LENIENT, /* invalid at layer 1, valid at layer 2: a documented leniency */
    X_PROF     /* valid at layer 1, invalid at layer 2 */
} Expect;

static FILE *manifest;
static const char *out_dir;
static int emitted, emitted_valid, mismatches;

static const char *expect_name(Expect x) {
    return x == X_VALID ? "valid/valid" : x == X_STD ? "invalid/invalid"
         : x == X_LENIENT ? "invalid/valid" : "valid/invalid";
}

/* Emit one vector: write the file, label it, append a manifest row, and
 * check the label and the reason against the mutant's expectation (see
 * Expect). */
static void emit(Bytes b, cf_kind kind, const char *name, const char *field,
                 const char *note, const char *companions, Expect expect,
                 const char *expected_reason, const char *profile_reason) {
    char file[256];
    Label l = label(b.data, b.len, kind);
    const char *reason = !l.std_ok ? l.std_reason : !l.prof_ok ? l.prof_reason : "-";
    int as_expected = expect == X_VALID   ? (l.std_ok && l.prof_ok)
                    : expect == X_STD     ? (!l.std_ok && !l.prof_ok)
                    : expect == X_LENIENT ? (!l.std_ok && l.prof_ok)
                    :                       (l.std_ok && !l.prof_ok);
    as_expected = as_expected &&
                  strcmp(reason, expected_reason != NULL ? expected_reason : "-") == 0 &&
                  (profile_reason == NULL ||
                   (l.prof_reason != NULL && strcmp(l.prof_reason, profile_reason) == 0));
    snprintf(file, sizeof file, "%s.%s", name, kind == CF_JP2 ? "jp2" : "jpx");
    write_file(out_dir, file, b);
    fprintf(manifest, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", file,
            kind == CF_JP2 ? "jp2" : "jpx",
            l.std_ok ? "valid" : "invalid",
            l.prof_ok ? "valid" : "invalid",
            reason,
            field ? field : "-",
            note ? note : "-",
            companions ? companions : "-",
            l.prof_ok ? "-" : l.prof_reason);
    emitted++;
    if (l.std_ok && l.prof_ok) emitted_valid++;
    if (!as_expected) {
        mismatches++;
        fprintf(stderr, "vectors: %s: expected %s (%s", file, expect_name(expect),
                expected_reason != NULL ? expected_reason : "-");
        if (profile_reason != NULL) fprintf(stderr, "; layer 2: %s", profile_reason);
        fprintf(stderr, "), labeled %s/%s (%s; layer 2: %s)\n",
                l.std_ok ? "valid" : "invalid", l.prof_ok ? "valid" : "invalid", reason,
                l.prof_reason != NULL ? l.prof_reason : "-");
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

SETTER(siz_rsiz,   cs_of(f, box)->siz.body.fixed.rsiz)
SETTER(siz_xsiz,   cs_of(f, box)->siz.body.fixed.xsiz)
SETTER(siz_ysiz,   cs_of(f, box)->siz.body.fixed.ysiz)
SETTER(siz_xosiz,  cs_of(f, box)->siz.body.fixed.xosiz)
SETTER(siz_yosiz,  cs_of(f, box)->siz.body.fixed.yosiz)
SETTER(siz_xtsiz,  cs_of(f, box)->siz.body.fixed.xtsiz)
SETTER(siz_ytsiz,  cs_of(f, box)->siz.body.fixed.ytsiz)
SETTER(siz_xtosiz, cs_of(f, box)->siz.body.fixed.xtosiz)
SETTER(siz_ytosiz, cs_of(f, box)->siz.body.fixed.ytosiz)
SETTER(siz_csiz,   cs_of(f, box)->siz.body.fixed.csiz)
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
    const char *reason;    /* the manifest reason (see Expect) */
    int only_kind;         /* 0 = any base, CF_JP2 / CF_JPX otherwise */
} FieldMutant;

static const FieldMutant field_mutants[] = {
    { "siz.rsiz", set_siz_rsiz, 2, "table A.10 value 2 (profile 1)", 0, X_VALID, NULL, 0 },
    { "siz.rsiz", set_siz_rsiz, 32768,
      "T.801 extensions (Table A.2): not in a JP2 file (T.800 I.5.4, Table A.10)", 0, X_LENIENT,
      "jp2.rsiz", CF_JP2 },
    { "siz.rsiz", set_siz_rsiz, 32768, "T.801 extensions (Table A.2): valid in a JPX file", 0,
      X_VALID, NULL, CF_JPX },
    { "siz.rsiz", set_siz_rsiz, 1, "table A.10 value 1 (profile 0)", 0, X_VALID, NULL, 0 },
    { "siz.rsiz", set_siz_rsiz, 3, "reserved (Table A.10); the server keeps any Rsiz", 0,
      X_LENIENT, "siz.rsiz", 0 },
    { "siz.xsiz", set_siz_xsiz, 0, "min-1", 0, X_STD, "decode", 0 },
    { "siz.xsiz", set_siz_xsiz, PROFILE_MAX_DIMENSION + 1, "profile max+1", 0, X_PROF, "decode", 0 },
    { "siz.xsiz", set_siz_xsiz, 4294967295u,
      "standard max; profile: beyond Siz-Profile and the tile", 0, X_PROF, "decode", 0 },
    { "siz.ysiz", set_siz_ysiz, 0, "min-1", 0, X_STD, "decode", 0 },
    { "siz.ysiz", set_siz_ysiz, PROFILE_MAX_DIMENSION + 1, "profile max+1", 0, X_PROF, "decode", 0 },
    { "siz.xosiz", set_siz_xosiz, 1, "profile max+1; standard valid", 0, X_PROF, "decode", 0 },
    { "siz.xosiz", set_siz_xosiz, 4, "== xsiz: empty image", 0, X_STD, "siz.origin-inside", 0 },
    { "siz.yosiz", set_siz_yosiz, 1, "profile max+1; standard valid", 0, X_PROF, "decode", 0 },
    { "siz.xtsiz", set_siz_xtsiz, 0, "min-1", 0, X_STD, "decode", 0 },
    { "siz.xtsiz", set_siz_xtsiz, 3, "below xsiz: two tiles", 0, X_PROF, "siz.single-tile", 0 },
    { "siz.ytsiz", set_siz_ytsiz, 3, "below ysiz: two tiles", 0, X_PROF, "siz.single-tile", 0 },
    { "siz.xtosiz", set_siz_xtosiz, 1, "tile origin past image origin", 0, X_STD,
      "siz.tile-origin", 0 },
    { "siz.ytosiz", set_siz_ytosiz, 1, "tile origin past image origin", 0, X_STD,
      "siz.tile-origin", 0 },
    { "siz.csiz", set_siz_csiz, 0, "min-1", 0, X_STD, "decode", 0 },
    { "siz.csiz", set_siz_csiz, 2, "count mismatch", 0, X_STD, "siz.csiz-count", 0 },
    { "siz.csiz", set_siz_csiz, 16385, "max+1 (and count mismatch)", 0, X_STD, "decode", 0 },
    { "siz.component.depthMinus1", set_siz_depth, 38, "max+1 (39-bit depth)", 0, X_STD, "decode", 0 },
    { "siz.component.xrsiz", set_siz_xrsiz, 0, "min-1", 0, X_STD, "decode", 0 },
    { "siz.component.yrsiz", set_siz_yrsiz, 0, "min-1", 0, X_STD, "decode", 0 },
    { "cod.scod.reserved", set_cod_reserved, 1, "reserved bit set", 0, X_STD, "decode", 0 },
    { "cod.scod.sopMarkers", set_cod_sop, 1, "SOP markers outside served packet representation", 0,
      X_PROF, "decode", 0 },
    { "cod.sgcod.progression", set_cod_progression, 5, "max+1", 0, X_STD, "decode", 0 },
    { "cod.sgcod.progression", set_cod_progression, 3, "PCRL with unit sampling: valid", 0, X_VALID,
      NULL, 0 },
    { "cod.sgcod.layers", set_cod_layers, 0, "min-1", 0, X_STD, "decode", 0 },
    { "cod.sgcod.mct", set_cod_mct, 2, "max+1", 0, X_STD, "decode", 0 },
    { "cod.sgcod.mct", set_cod_mct, 1, "MCT requires three components", 0, X_STD,
      "siz.mct-components", 0 },
    { "cod.spcod.levels", set_cod_levels, 33, "max+1", 0, X_STD, "decode", 0 },
    { "cod.spcod.cbWidthExp", set_cod_cbw, 9, "max+1", 0, X_STD, "decode", 0 },
    { "cod.spcod.cbWidthExp", set_cod_cbw, 5, "5+4 = 9 > 8: code-block area", 0, X_STD,
      "cod.codeblock-area", 0 },
    { "cod.spcod.cbHeightExp", set_cod_cbh, 9, "max+1", 0, X_STD, "decode", 0 },
    { "cod.spcod.cbStyle", set_cod_cbstyle, 63, "all Part 1 bits: valid", 0, X_VALID, NULL, 0 },
    { "cod.spcod.cbStyle", set_cod_cbstyle, 64, "reserved bit 6 (HTJ2K flag)", 0, X_STD, "decode", 0 },
    { "cod.spcod.cbStyle", set_cod_cbstyle, 255, "all bits", 0, X_STD, "decode", 0 },
    { "cod.spcod.transform", set_cod_transform, 2, "max+1", 0, X_STD, "decode", 0 },
    { "cod.spcod.precincts.lowest.ppx", set_cod_ppx_lowest, 0, "zero at r=0: valid", 1, X_VALID,
      NULL, 0 },
    { "cod.spcod.precincts.higher[0].ppx", set_cod_ppx_higher, 0, "zero above r=0 (Table A.21)", 1,
      X_STD, "cod.precincts-higher-zero", 0 },
    { "cod.spcod.precincts.higher[0].ppy", set_cod_ppy_higher, 0, "zero above r=0 (Table A.21)", 1,
      X_STD, "cod.precincts-higher-zero", 0 },
    { "sot.lsot", set_sot_lsot, 11, "Lsot != 10", 0, X_STD, "decode", 0 },
    { "sot.isot", set_sot_isot, 1, "tile 1 of a one-tile grid (A.4.2)", 0, X_STD,
      "sot.isot-range", 0 },
    { "sot.isot", set_sot_isot, 65535, "standard max+1", 0, X_STD, "decode", 0 },
    { "sot.tpsot", set_sot_tpsot, 1, "first tile-part index 1: sequence", 0, X_STD,
      "sot.tpsot-sequence", 0 },
    { "sot.tpsot", set_sot_tpsot, 255, "reserved", 0, X_STD, "decode", 0 },
    { "sot.tnsot", set_sot_tnsot, 0, "unspecified count: valid", 0, X_VALID, NULL, 0 },
    { "sot.tnsot", set_sot_tnsot, 2, "count 2 with one tile-part", 0, X_STD, "sot.tnsot-count", 0 },
    { "plt.zplt", set_plt_zplt, 1, "first PLT index is not zero", 0, X_STD, "plt.zplt-index", 0 },
    { "plt.iplt.b0.bits", set_iplt_bits, 0,
      "packet length 0 (with the headers in the data a packet has at least one byte)", 0, X_STD,
      "plt.zero-length", 0 },
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
/* Room for n tile-parts after COD and QCD in Codestream.segments, whose
 * corpus bound (SIZE (1..80)) the generated array has. */
static void check_tile_part_room(const Codestream *cs, int n) {
    if (n < 1 || 2 + n > CAPACITY(cs->segments.arr))
        die("more tile-parts than Codestream.segments holds");
}
/* n tile-parts of one layer each, TNsot n. */
static void rule_tile_parts_n(Jp2Family *f, int box, int n) {
    Codestream *cs = cs_of(f, box);
    int i;
    check_tile_part_room(cs, n);
    cod_of(f, box)->sgcod.layers = n;
    for (i = 1; i < n; ++i) cs->segments.arr[2 + i] = cs->segments.arr[2];
    for (i = 0; i < n; ++i) {
        cs->segments.arr[2 + i].tilePart.tpsot = i;
        cs->segments.arr[2 + i].tilePart.tnsot = n;
    }
    cs->segments.nCount = 2 + n;
}
static void rule_tile_parts_limit(Jp2Family *f, int box) {
    rule_tile_parts_n(f, box, HV_PROFILE_TILE_PARTS);
}
static void rule_jpx_jp2c_before_jpch(Jp2Family *f, int box) {
    (void) box;                                       /* jP ftyp rreq jp2h jp2c jpch jpch jp2c */
    move_box(f, box_index(f, TopPayload_jp2c_PRESENT, 0),
             box_index(f, TopPayload_jpch_PRESENT, 0));
}
static void rule_jpx_missing_jp2c(Jp2Family *f, int box) {
    (void) box;                                       /* two jpch, one jp2c */
    remove_box(f, box_index(f, TopPayload_jp2c_PRESENT, 1));
}
static void rule_jpx_no_jpch(Jp2Family *f, int box) {
    (void) box;                                       /* jP ftyp rreq jp2h jp2c jp2c */
    remove_box(f, box_index(f, TopPayload_jpch_PRESENT, 0));
    remove_box(f, box_index(f, TopPayload_jpch_PRESENT, 0));
}
static void rule_tile_parts_over_limit(Jp2Family *f, int box) {
    rule_tile_parts_n(f, box, HV_PROFILE_TILE_PARTS + 1);
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
    e->b4.bits = 1;                                        /* 5 bytes: valid */
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
    s->fixed.xsiz = s->fixed.xtsiz = PROFILE_MAX_DIMENSION;
    s->fixed.ysiz = s->fixed.ytsiz = PROFILE_MAX_DIMENSION;
}
static void rule_xsiz_profile_limit(Jp2Family *f, int box) {
    /* At level 0 the default 2^15-wide precincts of a 2^31-wide image are
     * 65,536 packets. Keep the standard layer valid, and the profile but for
     * Xsiz, with tile-parts of PLTS PLT segments of ENTRIES entries (their
     * corpus bounds), and as much data. */
    enum { PACKETS = 65536, PLTS = 8, ENTRIES = 128, PARTS = PACKETS / (PLTS * ENTRIES) };
    Codestream *cs = cs_of(f, box);
    Siz *s = &cs->siz.body;
    const TilePartRest *rest = &cs->segments.arr[2].tilePart.rest;
    int part, marker, entry;

    _Static_assert((int) PARTS <= (int) HV_PROFILE_TILE_PARTS,
                   "more tile-parts than the profile allows");
    if (PLTS > CAPACITY(rest->headers.arr) ||
        ENTRIES > CAPACITY(rest->headers.arr[0].plt.body.entries.arr) ||
        PLTS * ENTRIES > CAPACITY(rest->data.arr))
        die("rule_xsiz_profile_limit: tile-parts beyond the corpus bounds");
    check_tile_part_room(cs, PARTS);
    s->fixed.xsiz = s->fixed.xtsiz = PROFILE_MAX_DIMENSION + 1u;
    cs->segments.nCount = 2 + PARTS;
    for (part = 0; part < PARTS; part++) {
        MainSegment *segment = &cs->segments.arr[2 + part];
        TilePart *tile;
        if (part > 0) *segment = cs->segments.arr[2];
        tile = &segment->tilePart;
        tile->tpsot = part;
        tile->tnsot = PARTS;
        tile->rest.headers.nCount = PLTS;
        for (marker = 0; marker < PLTS; marker++) {
            Plt *plt = &tile->rest.headers.arr[marker].plt.body;
            tile->rest.headers.arr[marker].code = HV_PLT;
            tile->rest.headers.arr[marker].exist.plt = 1;
            plt->zplt = marker;
            plt->entries.nCount = ENTRIES;
            for (entry = 0; entry < ENTRIES; entry++) {
                Iplt *e = &plt->entries.arr[entry];
                memset(e, 0, sizeof *e);
                e->b0.bits = 1;
            }
        }
        tile->rest.data.nCount = PLTS * ENTRIES;
        memset(tile->rest.data.arr, 0, PLTS * ENTRIES);
    }
}
static void rule_iplt_too_long(Jp2Family *f, int box) {
    Iplt *e = &tp_of(f, box)->rest.headers.arr[0].plt.body.entries.arr[0];
    e->b0.bits = 2;                                        /* data holds 1 byte */
}
static void rule_subsampled(Jp2Family *f, int box) {
    cs_of(f, box)->siz.body.components.arr[0].xrsiz = 2;   /* profile: one shared precinct geometry */
}
/* The first tile-part of a codestream. */
static TilePart *first_tile_part(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    int i;
    for (i = 0; i < cs->segments.nCount; ++i)
        if (cs->segments.arr[i].exist.tilePart) return &cs->segments.arr[i].tilePart;
    die("no tile-part in the codestream");
    return NULL;
}
/* The opaque body of a main-header or tile-part header segment of `code`,
 * which the segment then carries. */
static Opaque *main_body(MainSegment *seg, int code) {
    memset(&seg->exist, 0, sizeof seg->exist);
    seg->code = code;
    switch (code) {
        case HV_COC: seg->exist.coc = 1; return &seg->coc.body;
        case HV_QCC: seg->exist.qcc = 1; return &seg->qcc.body;
        case HV_RGN: seg->exist.rgn = 1; return &seg->rgn.body;
        case HV_POC: seg->exist.poc = 1; return &seg->poc.body;
        case HV_TLM: seg->exist.tlm = 1; return &seg->tlm.body;
        case HV_PLM: seg->exist.plm = 1; return &seg->plm.body;
        case HV_PPM: seg->exist.ppm = 1; return &seg->ppm.body;
        case HV_CRG: seg->exist.crg = 1; return &seg->crg.body;
        default:     seg->exist.other = 1; return &seg->other.body;
    }
}
static Opaque *tile_body(TileSegment *ts, int code) {
    memset(&ts->exist, 0, sizeof ts->exist);
    ts->code = code;
    switch (code) {
        case HV_COC: ts->exist.coc = 1; return &ts->coc.body;
        case HV_QCC: ts->exist.qcc = 1; return &ts->qcc.body;
        case HV_RGN: ts->exist.rgn = 1; return &ts->rgn.body;
        case HV_POC: ts->exist.poc = 1; return &ts->poc.body;
        case HV_PPT: ts->exist.ppt = 1; return &ts->ppt.body;
        default:     ts->exist.other = 1; return &ts->other.body;
    }
}
/* A main-header segment of `code` with these n body bytes, before the
 * first tile-part. */
static void add_main_body(Jp2Family *f, int box, int code, const void *body, size_t n) {
    Codestream *cs = cs_of(f, box);
    int at, i;
    Opaque *o;
    for (at = 0; at < cs->segments.nCount && !cs->segments.arr[at].exist.tilePart; ++at)
        ;
    if (cs->segments.nCount >= CAPACITY(cs->segments.arr)) die("add_main_body: no room");
    for (i = cs->segments.nCount; i > at; --i) cs->segments.arr[i] = cs->segments.arr[i - 1];
    cs->segments.nCount++;
    o = main_body(&cs->segments.arr[at], code);
    OCTETS(o->data, body, n);
}
/* A segment of `code` with these n body bytes at the end of the first
 * tile-part's header. */
static void add_tile_body(Jp2Family *f, int box, int code, const void *body, size_t n) {
    TilePart *tp = first_tile_part(f, box);
    Opaque *o;
    if (tp->rest.headers.nCount >= CAPACITY(tp->rest.headers.arr)) die("add_tile_body: no room");
    o = tile_body(&tp->rest.headers.arr[tp->rest.headers.nCount++], code);
    OCTETS(o->data, body, n);
}
/* A COC for component c of the main COD's coding, with Scoc `scoc` and no
 * precinct sizes: Ccoc, Scoc, then SPcoc (Table A.15). */
static void add_coc(Jp2Family *f, int box, int c, int scoc, int transform) {
    const Spcod *sp = &cod_of(f, box)->spcod;
    unsigned char body[7];
    body[0] = (unsigned char) c;
    body[1] = (unsigned char) scoc;
    body[2] = (unsigned char) sp->levels;
    body[3] = (unsigned char) sp->cbWidthExp;
    body[4] = (unsigned char) sp->cbHeightExp;
    body[5] = (unsigned char) sp->cbStyle;
    body[6] = (unsigned char) transform;
    add_main_body(f, box, HV_COC, body, sizeof body);
}
/* A POC of one progression change: RSpoc, CSpoc, LYEpoc 1, REpoc, CEpoc,
 * Ppoc 0 (LRCP). */
static void add_poc(Jp2Family *f, int box, int rs, int cs, int re, int ce) {
    unsigned char body[7];
    body[0] = (unsigned char) rs;
    body[1] = (unsigned char) cs;
    body[2] = 0; body[3] = 1;
    body[4] = (unsigned char) re;
    body[5] = (unsigned char) ce;
    body[6] = 0;
    add_main_body(f, box, HV_POC, body, sizeof body);
}
/* The bytes of an Iplt entry. */
static size_t iplt_size(const Iplt *e) {
    return 1u + e->exist.b1 + e->exist.b2 + e->exist.b3 + e->exist.b4 + e->exist.b5 +
           e->exist.b6 + e->exist.b7 + e->exist.b8 + e->exist.b9;
}
/* Psot of a tile-part (A.4.2): SOT, its header's segments, SOD, the data. */
static uint32_t tile_part_length(const TilePart *tp) {
    size_t n = 12 + 2 + (size_t) tp->rest.data.nCount;
    int j, e;
    for (j = 0; j < tp->rest.headers.nCount; ++j) {
        const TileSegment *ts = &tp->rest.headers.arr[j];
        n += 4;
        if (ts->exist.plt) {
            n += 1;
            for (e = 0; e < ts->plt.body.entries.nCount; ++e)
                n += iplt_size(&ts->plt.body.entries.arr[e]);
        }
        if (ts->exist.cod) n += 10 + (size_t) ts->cod.body.spcod.precincts.nCount;
        if (ts->exist.qcd) n += 1 + (size_t) ts->qcd.body.spqcd.nCount;
        if (ts->exist.com) n += 2 + (size_t) ts->com.body.ccom.nCount;
        if (ts->exist.coc) n += (size_t) ts->coc.body.data.nCount;
        if (ts->exist.qcc) n += (size_t) ts->qcc.body.data.nCount;
        if (ts->exist.rgn) n += (size_t) ts->rgn.body.data.nCount;
        if (ts->exist.poc) n += (size_t) ts->poc.body.data.nCount;
        if (ts->exist.ppt) n += (size_t) ts->ppt.body.data.nCount;
        if (ts->exist.other) n += (size_t) ts->other.body.data.nCount;
        if (ts->exist.noSegment) n -= 2;
    }
    return (uint32_t) n;
}
/* The packets of the first tile-part: its PLT entries (one per packet in
 * the bases). */
static int packets_of(Jp2Family *f, int box) {
    return first_tile_part(f, box)->rest.headers.arr[0].plt.body.entries.nCount;
}
/* The packet headers of the first tile-part out of its data: each packet
 * of the bases is an empty packet, one byte of header (B.10.3) and no
 * body, so the data empties and every packet length is 0 (A.7.3: without
 * its header). */
static void headers_out_of_data(Jp2Family *f, int box) {
    TilePart *tp = first_tile_part(f, box);
    Plt *plt = &tp->rest.headers.arr[0].plt.body;
    int i;
    for (i = 0; i < plt->entries.nCount; ++i) memset(&plt->entries.arr[i], 0, sizeof plt->entries.arr[i]);
    tp->rest.data.nCount = 0;
}
/* A PPM (A.7.4) of Zppm `z` with the tile-part's packet headers, and
 * `runs` runs of Nppm, Ippm (the first with the headers, any other empty),
 * the first Nppm `excess` more than its headers. */
static void add_ppm_z(Jp2Family *f, int box, int z, int runs, int excess) {
    unsigned char body[1 + 2 * 4 + 64];
    int n = packets_of(f, box), k = 0, r;
    body[k++] = (unsigned char) z;                         /* Zppm */
    for (r = 0; r < runs; ++r) {
        int count = r == 0 ? n : 0;
        put32(body + k, (uint32_t) (count + (r == 0 ? excess : 0)));
        k += 4;
        memset(body + k, 0, (size_t) count);               /* empty packet headers */
        k += count;
    }
    headers_out_of_data(f, box);
    add_main_body(f, box, HV_PPM, body, (size_t) k);
}
/* A well-formed PPM: Zppm 0, no excess. */
static void add_ppm(Jp2Family *f, int box, int runs) { add_ppm_z(f, box, 0, runs, 0); }
static void rule_main_coc(Jp2Family *f, int box) { add_coc(f, box, 0, 0, 1); }
static void rule_main_poc(Jp2Family *f, int box) { add_poc(f, box, 0, 0, 33, 1); }
static void rule_main_ppm(Jp2Family *f, int box) { add_ppm(f, box, 1); }
static void rule_coc_short(Jp2Family *f, int box) {
    add_main_body(f, box, HV_COC, "\x00\x00\x00", 3);      /* Lcoc 5: 9 to 43 (Table A.22) */
}
static void rule_poc_short(Jp2Family *f, int box) {
    add_main_body(f, box, HV_POC, "\x00\x00\x00", 3);      /* Lpoc 5: 9 to 65,535 (Table A.32) */
}
static void rule_coc_component(Jp2Family *f, int box) { add_coc(f, box, 1, 0, 1); }
static void rule_coc_twice(Jp2Family *f, int box) {
    add_coc(f, box, 0, 0, 1);
    add_coc(f, box, 0, 0, 1);
}
static void rule_coc_precincts(Jp2Family *f, int box) { add_coc(f, box, 0, 1, 1); }
/* A COC as add_coc's, with code-block exponents summing to 9 (xcb + ycb
 * 13, A.6.1: at most 12). */
static void rule_coc_codeblock_area(Jp2Family *f, int box) {
    const Spcod *sp = &cod_of(f, box)->spcod;
    unsigned char body[7];
    body[0] = 0;
    body[1] = 0;
    body[2] = (unsigned char) sp->levels;
    body[3] = (unsigned char) (9 - sp->cbHeightExp);
    body[4] = (unsigned char) sp->cbHeightExp;
    body[5] = (unsigned char) sp->cbStyle;
    body[6] = 1;
    add_main_body(f, box, HV_COC, body, sizeof body);
}
/* A COC with Scoc 1 and the main COD's precinct sizes, PPx 0 at r = 1
 * (Table A.21: 0 only at r = 0). */
static void rule_coc_precincts_zero(Jp2Family *f, int box) {
    const Spcod *sp = &cod_of(f, box)->spcod;
    unsigned char body[7 + 34];
    int i, levels = (int) sp->levels;
    if (levels < 1 || sp->precincts.nCount != levels + 1)
        die("rule_coc_precincts_zero: needs the precinct base");
    body[0] = 0;
    body[1] = 1;
    body[2] = (unsigned char) sp->levels;
    body[3] = (unsigned char) sp->cbWidthExp;
    body[4] = (unsigned char) sp->cbHeightExp;
    body[5] = (unsigned char) sp->cbStyle;
    body[6] = (unsigned char) sp->transform;
    for (i = 0; i <= levels; ++i)
        body[7 + i] = (unsigned char) (sp->precincts.arr[i].ppy << 4 | sp->precincts.arr[i].ppx);
    body[8] &= 0xF0;
    add_main_body(f, box, HV_COC, body, 8 + (size_t) levels);
}
static void rule_poc_resolution(Jp2Family *f, int box) { add_poc(f, box, 1, 0, 1, 1); }
static void rule_poc_component(Jp2Family *f, int box) { add_poc(f, box, 0, 1, 33, 1); }
/* Two tile-parts, the second's header with a POC (RSpoc 0, CSpoc 0,
 * LYEpoc 2, REpoc 33, CEpoc 1, LRCP) and the first's without. */
static void rule_poc_later_part(Jp2Family *f, int box) {
    static const unsigned char body[7] = {0, 0, 0, 2, 33, 1, 0};
    TilePart *tp;
    Opaque *o;
    rule_two_tile_parts(f, box);
    tp = &cs_of(f, box)->segments.arr[3].tilePart;
    if (tp->rest.headers.nCount >= CAPACITY(tp->rest.headers.arr))
        die("rule_poc_later_part: no room");
    o = tile_body(&tp->rest.headers.arr[tp->rest.headers.nCount++], HV_POC);
    OCTETS(o->data, body, sizeof body);
}
static void rule_poc_twice(Jp2Family *f, int box) {
    add_poc(f, box, 0, 0, 33, 1);
    add_poc(f, box, 0, 0, 33, 1);
}
/* A QCC for component c: Cqcc, then the main QCD's body with Sqcc `sqcc`
 * and `n` SPqcc bytes of the QCD's first, the first ORed with `bits`. */
static void add_qcc_bits(Jp2Family *f, int box, int c, int sqcc, int n, int bits) {
    const Qcd *q = &cs_of(f, box)->segments.arr[1].qcd.body;
    unsigned char body[2 + 194];
    body[0] = (unsigned char) c;
    body[1] = (unsigned char) sqcc;
    memset(body + 2, q->spqcd.arr[0], (size_t) n);
    body[2] |= (unsigned char) bits;
    add_main_body(f, box, HV_QCC, body, 2 + (size_t) n);
}
static void add_qcc(Jp2Family *f, int box, int c, int sqcc, int n) {
    add_qcc_bits(f, box, c, sqcc, n, 0);
}
static int qcd_bytes(Jp2Family *f, int box) {
    return cs_of(f, box)->segments.arr[1].qcd.body.spqcd.nCount;
}
static void rule_qcc(Jp2Family *f, int box) { add_qcc(f, box, 0, 0x40, qcd_bytes(f, box)); }
static void rule_qcc_component(Jp2Family *f, int box) { add_qcc(f, box, 1, 0x40, qcd_bytes(f, box)); }
static void rule_qcc_twice(Jp2Family *f, int box) {
    add_qcc(f, box, 0, 0x40, qcd_bytes(f, box));
    add_qcc(f, box, 0, 0x40, qcd_bytes(f, box));
}
static void rule_qcc_length(Jp2Family *f, int box) { add_qcc(f, box, 0, 0x40, 2); }
static void rule_qcc_style(Jp2Family *f, int box) { add_qcc(f, box, 0, 0x43, qcd_bytes(f, box)); }
static void rule_qcc_reserved(Jp2Family *f, int box) {
    add_qcc_bits(f, box, 0, 0x40, qcd_bytes(f, box), 1);
}
static Qcd *qcd_of(Jp2Family *f, int box) { return &cs_of(f, box)->segments.arr[1].qcd.body; }
static void rule_qcd_style(Jp2Family *f, int box) { qcd_of(f, box)->sqcd = 0x43; }
static void rule_qcd_length(Jp2Family *f, int box) { qcd_of(f, box)->spqcd.nCount = 2; }
static void rule_qcd_reserved(Jp2Family *f, int box) { qcd_of(f, box)->spqcd.arr[0] |= 1; }
static void rule_qcd_expounded(Jp2Family *f, int box) {
    Qcd *q = qcd_of(f, box);                               /* scalar quantization, 5-3 */
    q->sqcd = 0x42;
    q->spqcd.nCount = 2;
    q->spqcd.arr[0] = 0x40;
    q->spqcd.arr[1] = 0x00;
}
static void rule_rgn(Jp2Family *f, int box) { add_main_body(f, box, HV_RGN, "\x00\x00\x05", 3); }
static void rule_rgn_component(Jp2Family *f, int box) {
    add_main_body(f, box, HV_RGN, "\x01\x00\x05", 3);
}
static void rule_rgn_twice(Jp2Family *f, int box) {
    add_main_body(f, box, HV_RGN, "\x00\x00\x05", 3);
    add_main_body(f, box, HV_RGN, "\x00\x00\x05", 3);
}
static void rule_rgn_style(Jp2Family *f, int box) { add_main_body(f, box, HV_RGN, "\x00\x01\x05", 3); }
/* A TLM of one entry, ST 1 and SP 1, for the one tile-part: Ztlm `z`,
 * Ttlm 0, Ptlm its Psot plus `off`. */
static void add_tlm(Jp2Family *f, int box, int z, int stlm, int off) {
    unsigned char body[7];
    body[0] = (unsigned char) z;
    body[1] = (unsigned char) stlm;
    body[2] = 0;
    put32(body + 3, tile_part_length(first_tile_part(f, box)) + (uint32_t) off);
    add_main_body(f, box, HV_TLM, body, sizeof body);
}
static void rule_tlm(Jp2Family *f, int box) { add_tlm(f, box, 0, 0x50, 0); }
static void rule_tlm_length(Jp2Family *f, int box) { add_tlm(f, box, 0, 0x50, 1); }
static void rule_tlm_index(Jp2Family *f, int box) { add_tlm(f, box, 1, 0x50, 0); }
static void rule_tlm_reserved(Jp2Family *f, int box) { add_tlm(f, box, 0, 0xD0, 0); }
/* A PLM of the one tile-part's packet lengths: Zplm `z`, Nplm, then Nplm
 * lengths of 1 (the bases' packets), the first `first`; `extra` more runs
 * of Nplm 0. */
static void add_plm_z(Jp2Family *f, int box, int z, int nplm, int first, int extra) {
    unsigned char body[2 + 64];
    int n = packets_of(f, box), k = 0, i;
    body[k++] = (unsigned char) z;
    body[k++] = (unsigned char) nplm;
    for (i = 0; i < n; ++i) body[k++] = (unsigned char) (i == 0 ? first : 1);
    for (i = 0; i < extra; ++i) body[k++] = 0;
    add_main_body(f, box, HV_PLM, body, (size_t) k);
}
static void add_plm(Jp2Family *f, int box, int nplm, int first, int extra) {
    add_plm_z(f, box, 0, nplm, first, extra);
}
static void rule_plm(Jp2Family *f, int box) { add_plm(f, box, packets_of(f, box), 1, 0); }
static void rule_plm_index(Jp2Family *f, int box) {
    add_plm_z(f, box, 1, packets_of(f, box), 1, 0);
}
static void rule_plm_zero(Jp2Family *f, int box) { add_plm(f, box, packets_of(f, box), 0, 0); }
static void rule_plm_coverage(Jp2Family *f, int box) { add_plm(f, box, packets_of(f, box), 2, 0); }
static void rule_plm_tile_parts(Jp2Family *f, int box) { add_plm(f, box, packets_of(f, box), 1, 1); }
static void rule_plm_length(Jp2Family *f, int box) { add_plm(f, box, packets_of(f, box) + 1, 1, 0); }
static void rule_ppm_no_headers(Jp2Family *f, int box) {
    add_main_body(f, box, HV_PPM, "\x00\x00\x00\x00\x00", 5);   /* Nppm 0, the headers in the data */
}
static void rule_ppm_runs(Jp2Family *f, int box) { add_ppm(f, box, 2); }
static void rule_ppm_index(Jp2Family *f, int box) { add_ppm_z(f, box, 1, 1, 0); }
static void rule_ppm_length(Jp2Family *f, int box) { add_ppm_z(f, box, 0, 1, 1); }
static void rule_ppm_ppt(Jp2Family *f, int box) {
    add_ppm(f, box, 1);
    add_tile_body(f, box, HV_PPT, "\x00\x00", 2);
}
/* The tile-part's packet headers in a PPT (A.7.5): Zppt, then the headers. */
static void add_ppt(Jp2Family *f, int box, int z) {
    unsigned char body[1 + 64];
    int n = packets_of(f, box);
    body[0] = (unsigned char) z;
    memset(body + 1, 0, (size_t) n);
    add_tile_body(f, box, HV_PPT, body, 1 + (size_t) n);
}
static void rule_ppt(Jp2Family *f, int box) {
    headers_out_of_data(f, box);
    add_ppt(f, box, 0);
}
static void rule_ppt_index(Jp2Family *f, int box) {
    headers_out_of_data(f, box);
    add_ppt(f, box, 0);
    add_ppt(f, box, 0);
}
static void rule_crg(Jp2Family *f, int box) {
    add_main_body(f, box, HV_CRG, "\x80\x00\x80\x00", 4);
}
static void rule_crg_twice(Jp2Family *f, int box) {
    rule_crg(f, box);
    rule_crg(f, box);
}
static void rule_crg_length(Jp2Family *f, int box) { add_main_body(f, box, HV_CRG, "\x80\x00", 2); }
/* A marker `code` in front of the first packet of the first tile-part's
 * data: an SOP marker segment (FF91, Lsop, Nsop 0) or EPH (FF92), whose
 * bytes the first packet length counts. */
static void marker_in_data(Jp2Family *f, int box, const void *marker, size_t n) {
    TilePart *tp = first_tile_part(f, box);
    Iplt *e = &tp->rest.headers.arr[0].plt.body.entries.arr[0];
    memmove(tp->rest.data.arr + n, tp->rest.data.arr, (size_t) tp->rest.data.nCount);
    memcpy(tp->rest.data.arr, marker, n);
    tp->rest.data.nCount += (int) n;
    e->b0.bits += (asn1SccUint) n;
}
static void rule_sop_unsignalled(Jp2Family *f, int box) {
    marker_in_data(f, box, "\xFF\x91\x00\x04\x00\x00", 6);
}
static void rule_sop_length(Jp2Family *f, int box) {
    cod_of(f, box)->scod.sopMarkers = 1;
    marker_in_data(f, box, "\xFF\x91\x00\x05\x00\x00", 6);
}
static void rule_eph_unsignalled(Jp2Family *f, int box) { marker_in_data(f, box, "\xFF\x92", 2); }
static void rule_eph_with_ppm(Jp2Family *f, int box) {
    cod_of(f, box)->scod.ephMarkers = 1;
    add_ppm(f, box, 1);
    {
        TilePart *tp = first_tile_part(f, box);
        tp->rest.data.arr[0] = 0xFF;
        tp->rest.data.arr[1] = 0x92;
        tp->rest.data.nCount = 2;
        tp->rest.headers.arr[0].plt.body.entries.arr[0].b0.bits = 2;
    }
}
/* Three components, the multiple component transform, and a COC giving
 * component 1 the 9-7 transform (and a QCC to match): G.2 and G.3 transform
 * components 0 to 2 with one filter. */
static void rule_mct_transform(Jp2Family *f, int box) {
    Siz *s = &cs_of(f, box)->siz.body;
    s->fixed.csiz = 3;
    s->components.nCount = 3;
    s->components.arr[1] = s->components.arr[2] = s->components.arr[0];
    cod_of(f, box)->sgcod.mct = 1;
    add_coc(f, box, 1, 0, 0);
    add_main_body(f, box, HV_QCC, "\x01\x41\x40\x00", 4);  /* scalar derived */
}
static void rule_profile0_cbstyle(Jp2Family *f, int box) {
    cs_of(f, box)->siz.body.fixed.rsiz = 1;
    cod_of(f, box)->spcod.cbStyle = 1;                     /* selective bypass: a = 1 */
}
static void rule_profile1_codeblock(Jp2Family *f, int box) {
    cs_of(f, box)->siz.body.fixed.rsiz = 2;
    cod_of(f, box)->spcod.cbWidthExp = 5;                  /* xcb 7 > 6 */
    cod_of(f, box)->spcod.cbHeightExp = 3;
}
/* Rsiz 2 on the precinct base (NL 1, D 2), one tile `w` wide: Table A.45's
 * LL resolution, floor(tx1 / D) - floor(tx0 / D), is floor(w / 2). */
static void profile1_width(Jp2Family *f, int box, uint32_t w) {
    Codestream *cs = cs_of(f, box);
    cs->siz.body.fixed.rsiz = 2;
    cs->siz.body.fixed.xsiz = w;
    cs->siz.body.fixed.xtsiz = w;
}
static void rule_profile1_ll_257(Jp2Family *f, int box) { profile1_width(f, box, 257); }
static void rule_profile1_ll_258(Jp2Family *f, int box) { profile1_width(f, box, 258); }
static void rule_zplt_permuted(Jp2Family *f, int box) {
    TilePart *tp = tp_of(f, box);                          /* precinct base: two packets */
    TileSegment first;
    rule_two_plt_markers(f, box);
    first = tp->rest.headers.arr[0];                       /* Zplt 1 before Zplt 0 */
    tp->rest.headers.arr[0] = tp->rest.headers.arr[1];
    tp->rest.headers.arr[1] = first;
}
static Ftyp *ftyp_of(Jp2Family *f) {
    return &f->boxes.arr[box_index(f, TopPayload_ftyp_PRESENT, 0)].payload.u.ftyp;
}
static void rule_ftyp_brand(Jp2Family *f, int box) {
    (void) box;
    ftyp_of(f)->header.brand = be32((const unsigned char *) "abcd");          /* wrong brand */
}
static void rule_ftyp_compat(Jp2Family *f, int box) {
    (void) box;
    ftyp_of(f)->compat.arr[0] = be32((const unsigned char *) "abcd");  /* brand missing from list */
}
static Superbox *jpch_of(Jp2Family *f);
static void rule_ftyp_two_compat(Jp2Family *f, int box) {
    Ftyp *ftyp = ftyp_of(f);                                           /* brand second: valid */
    (void) box;
    ftyp->compat.nCount = 2;
    ftyp->compat.arr[1] = ftyp->compat.arr[0];
    ftyp->compat.arr[0] = be32((const unsigned char *) "jpxb");
    /* A baseline JPX file: the first codestream's header is jp2h's (T.801
     * M.9.2.7), so its jpch holds no ihdr. */
    if (is_jpx(f)) jpch_of(f)->children.nCount = 0;
}
static Superbox *jpch_of(Jp2Family *f) {                   /* the first jpch */
    return &f->boxes.arr[box_index(f, TopPayload_jpch_PRESENT, 0)].payload.u.jpch;
}
static void rule_jp2c_in_jpch(Jp2Family *f, int box) {
    Superbox *sb = jpch_of(f);
    InnerBox *c = &sb->children.arr[sb->children.nCount++];
    (void) box;
    c->payload.kind = InnerPayload_jp2c_PRESENT;
    OCTETS(c->payload.u.jp2c.data, "\x00", 1);
}
static void rule_missing_rreq(Jp2Family *f, int box) {
    (void) box;
    remove_box(f, box_index(f, TopPayload_rreq_PRESENT, 0));
}
static void rule_unknown_box(Jp2Family *f, int box) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    (void) box;
    b->payload.kind = TopPayload_other_PRESENT;
    OCTETS(b->payload.u.other.data, "\x01", 1);
}
static void rule_missing_signature(Jp2Family *f, int box) {
    (void) box;
    remove_box(f, box_index(f, TopPayload_jP_PRESENT, 0));
}
static void rule_two_tiles(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    cs->siz.body.fixed.xtsiz = BASE_W / 2;                 /* two tiles, one tile-part each */
    cs->segments.arr[3] = cs->segments.arr[2];
    cs->segments.arr[3].tilePart.isot = 1;
    cs->segments.nCount = 4;
}
static void rule_tile_cod_mct(Jp2Family *f, int box) {
    rule_tile_cod(f, box);                                 /* one component */
    tp_of(f, box)->rest.headers.arr[1].cod.body.sgcod.mct = 1;
}
/* The origin past the first tile: XOsiz 2, XTsiz 2 from XTOsiz 0. */
static void rule_tile_short_of_origin(Jp2Family *f, int box) {
    SizFixed *s = &cs_of(f, box)->siz.body.fixed;
    s->xosiz = 2;
    s->xtsiz = 2;
}
/* MCT over three components, the second sampled 2:1 horizontally. */
static void rule_mct_geometry(Jp2Family *f, int box) {
    Siz *s = &cs_of(f, box)->siz.body;
    s->fixed.csiz = 3;
    s->components.nCount = 3;
    s->components.arr[1] = s->components.arr[2] = s->components.arr[0];
    s->components.arr[1].xrsiz = 2;
    cod_of(f, box)->sgcod.mct = 1;
}
/* Two tile-parts, both declaring TNsot 1: the second's TPsot is 1. */
static void rule_tpsot_at_tnsot(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    rule_two_tile_parts(f, box);
    cs->segments.arr[2].tilePart.tnsot = 1;
    cs->segments.arr[3].tilePart.tnsot = 1;
}
static void rule_tile_qcd_twice(Jp2Family *f, int box) {
    rule_tile_qcd(f, box);
    rule_tile_qcd(f, box);
}
static void rule_tile_qcd_second_part(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    TilePart *tp;
    TileSegment *ts;
    rule_two_tile_parts(f, box);
    tp = &cs->segments.arr[3].tilePart;                 /* QCD in tile-part 1 */
    ts = &tp->rest.headers.arr[tp->rest.headers.nCount++];
    ts->code = HV_QCD;
    ts->exist.qcd = 1;
    ts->qcd.body = cs->segments.arr[1].qcd.body;
}
/* An unknown box between the signature and ftyp. */
static void rule_ftyp_third(Jp2Family *f, int box) {
    rule_unknown_box(f, box);
    move_box(f, f->boxes.nCount - 1, 1);
}
static void rule_signature_only(Jp2Family *f, int box) {
    (void) box;
    f->boxes.nCount = 1;
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

/* `name` is the rule the mutant breaks (the manifest's `field`), or the
 * shape it builds when it is valid or breaks a type; `reason` says what
 * rejects it (see Expect). */
typedef struct {
    const char *name;
    RuleMutator apply;
    const char *note;
    int only_kind;         /* 0 = any base with a codestream, CF_JP2 / CF_JPX otherwise */
    int needs_precincts;   /* 1 = apply to the explicit-precinct base (two packets) */
    Expect expect;
    const char *reason;           /* the manifest reason */
    const char *profile_reason;   /* the layer-2 reason where it is checked, or NULL */
} RuleMutant;

/* Both rule tables use array positions in fixture names; append new entries. */
static const RuleMutant rule_mutants[] = {
    { "codestream.one-cod-before-sot", rule_second_cod, "two COD in main header", 0, 0, X_STD,
      "codestream.one-cod-before-sot", NULL },
    { "codestream.one-qcd-before-sot", rule_second_qcd, "two QCD in main header", 0, 0, X_STD,
      "codestream.one-qcd-before-sot", NULL },
    { "codestream.one-qcd-before-sot", rule_no_qcd, "no QCD", 0, 0, X_STD,
      "codestream.one-qcd-before-sot", NULL },
    { "codestream.segment-after-sot", rule_qcd_after_sot, "QCD after the tile-part data", 0, 0,
      X_STD, "codestream.segment-after-sot", NULL },
    { "codestream.no-plt", rule_no_plt, "no PLT: standard valid, profile invalid", 0, 0, X_PROF,
      "codestream.no-plt", NULL },
    { "cod.precincts-count", rule_precinct_count, "levels+2 precinct bytes", 0, 0, X_STD,
      "cod.precincts-count", NULL },
    { "codestream.no-tile-part", rule_no_tile_part, "main header only", 0, 0, X_STD,
      "codestream.no-tile-part", NULL },
    { "sot.two-tile-parts", rule_two_tile_parts, "two tile-parts: valid", 0, 0, X_VALID, NULL,
      NULL },
    { "sot.tnsot-late", rule_tnsot_declared_late, "TNsot 0 then 2: valid", 0, 0, X_VALID, NULL,
      NULL },
    { "codestream.tile-part-limit", rule_tile_parts_limit, "64 tile-parts: profile maximum, valid",
      0, 0, X_VALID, NULL, NULL },
    { "plt.two-markers", rule_two_plt_markers, "packet lengths split over two PLT: valid", 0, 1,
      X_VALID, NULL, NULL },
    { "tile.header-marker", rule_com_in_tile_header, "COM in the tile-part header: profile invalid",
      0, 0, X_PROF, "decode", NULL },
    { "jpx.box-order", rule_jpx_jp2c_before_jpch,
      "jp2c before the jpch boxes: valid (counts match)", CF_JPX, 0, X_VALID, NULL, NULL },
    { "jpx.reader-requirements", rule_missing_rreq,
      "no rreq box: standard invalid, profile accepted", CF_JPX, 0, X_LENIENT,
      "jpx.reader-requirements", NULL },
    { "file.unknown-box", rule_unknown_box, "unknown top-level box: valid and skipped", 0, 0,
      X_VALID, NULL, NULL },
    { "jpx.codestream-count", rule_jpx_missing_jp2c, "two jpch, one jp2c (T.801 M.11.6)", CF_JPX, 0,
      X_STD, "jpx.codestream-count", NULL },
    { "plt.trailing-zero", rule_trailing_zero_iplt,
      "extra zero Iplt after the last packet: standard invalid, profile accepted for deployed "
      "files", 0, 0, X_LENIENT, "plt.zero-length", NULL },
    { "jpx.no-jpch", rule_jpx_no_jpch, "jp2c boxes without jpch: profile invalid", CF_JPX, 0,
      X_PROF, "jpx.no-jpch", NULL },
    { "jpch.nested-jp2c", rule_jp2c_in_jpch, "jp2c inside a jpch superbox", CF_JPX, 0, X_STD,
      "jpch.nested-jp2c", NULL },
    { "sot.tnsot-inconsistent", rule_tnsot_inconsistent,
      "second SOT declares 3 tile-parts, first 2", 0, 0, X_STD, "sot.tnsot-inconsistent", NULL },
    { "codestream.tile-part-limit", rule_tile_parts_over_limit, "65 tile-parts: profile invalid", 0,
      0, X_PROF, "decode", NULL },
    { "main.com", rule_com_segment, "COM segment: valid", 0, 0, X_VALID, NULL, NULL },
    { "plt.iplt-five-bytes", rule_iplt_five_bytes, "5-byte Iplt encoding value 1: valid", 0, 0,
      X_VALID, NULL, NULL },
    { "plt.sum-exceeds-data", rule_iplt_too_long, "packet length beyond tile-part data", 0, 0,
      X_STD, "plt.coverage", NULL },
    { "plt.sum-short", rule_iplt_too_short, "tile-part byte no PLT entry covers", 0, 0, X_STD,
      "plt.coverage", NULL },
    { "tile.header-coding-default", rule_tile_cod, "COD in the first tile-part: profile invalid", 0,
      0, X_PROF, "decode", NULL },
    { "tile.header-coding-default", rule_tile_qcd, "QCD in the first tile-part: profile invalid", 0,
      0, X_PROF, "decode", NULL },
    { "tile.cod-once", rule_tile_cod_twice, "two COD in the tile-part header", 0, 0, X_STD,
      "tile.cod-once", NULL },
    { "tile.cod-once", rule_tile_cod_second_part, "COD in the second tile-part", 0, 0, X_STD,
      "tile.cod-once", NULL },
    { "codestream.packet-count", rule_packet_count_overflow, "2^32 packets: profile invalid", 0, 0,
      X_PROF, "codestream.packet-count", NULL },
    { "plt.iplt-six-bytes", rule_iplt_six_bytes, "6-byte Iplt encoding value 1: valid", 0, 0,
      X_VALID, NULL, NULL },
    { "siz.component-sampling", rule_subsampled, "2:1 sampling: profile invalid", 0, 0, X_PROF,
      "decode", NULL },
    { "main.packet-layout-override", rule_main_coc, "COC in the main header: profile invalid", 0, 0,
      X_PROF, "decode", NULL },
    { "main.packet-layout-override", rule_main_poc, "POC in the main header: profile invalid", 0, 0,
      X_PROF, "decode", NULL },
    { "file.ftyp-brand", rule_ftyp_brand,
      "ftyp brand 'abcd', 'jp2 ' in the list: a JP2 file (T.800 I.5.2), profile invalid", CF_JP2,
      0, X_PROF, "file.ftyp-brand", NULL },
    { "file.ftyp-compatibility", rule_ftyp_compat, "brand absent from the compatibility list", 0, 0,
      X_STD, "file.ftyp-compatibility", NULL },
    { "file.ftyp-compatibility", rule_ftyp_two_compat,
      "brand second in the compatibility list: valid", 0, 0, X_VALID, NULL, NULL },
    { "file.signature", rule_missing_signature, "no jP box (T.800 I.4)", CF_JP2, 0, X_STD,
      "file.signature", NULL },
    { "jp2.one-codestream", rule_two_jp2c, "two jp2c in .jp2", CF_JP2, 0, X_PROF,
      "jp2.one-codestream", NULL },
    { "plt.boundaries", rule_plt_boundaries,
      "T.800 A.7.3: lengths 1,127,128,129 across PLT and tile-part boundaries", 0, 0, X_VALID, NULL,
      NULL },
    { "plt.second-part-short", rule_plt_second_part_short,
      "second tile-part PLT sum one byte short", 0, 0, X_STD, "plt.coverage", NULL },
    { "plt.second-part-long", rule_plt_second_part_long,
      "second tile-part PLT sum one byte too long", 0, 0, X_STD, "plt.coverage", NULL },
    { "plt.packet-count", rule_merged_plt_packets,
      "two packet lengths merged into one entry with unchanged data", 0, 1, X_STD,
      "plt.packet-count", NULL },
    { "plt.padding-position", rule_middle_zero_iplt, "zero Iplt between two logical packets", 0, 1,
      X_STD, "plt.zero-length", "plt.padding-position" },
    { "plt.zero-length", rule_zero_logical_iplt, "zero Iplt for the only logical packet", 0, 0,
      X_STD, "plt.zero-length", NULL },
    { "plt.packet-count", rule_extra_nonzero_iplt, "nonzero Iplt beyond the only logical packet", 0,
      0, X_STD, "plt.packet-count", NULL },
    { "plt.value-overflow", rule_iplt_value_overflow, "Iplt value 2^64+1 wraps to data length 1", 0,
      0, X_STD, "plt.value-overflow", NULL },
    { "plt.sum-overflow", rule_iplt_sum_overflow, "Iplt lengths UINT64_MAX+3 wrap to data length 2",
      0, 1, X_STD, "plt.coverage", NULL },
    { "main.packet-headers-moved", rule_main_ppm, "PPM in the main header: profile invalid", 0, 0,
      X_PROF, "decode", NULL },
    { "sot.two-tiles", rule_two_tiles,
      "two tiles, TPsot 0 and TNsot 1 in each: standard valid, profile invalid", 0, 0, X_PROF,
      "decode", NULL },
    { "siz.mct-components", rule_tile_cod_mct, "MCT in a tile-part COD with one component", 0, 0,
      X_STD, "siz.mct-components", NULL },
    { "ftbl.one-flst", rule_jp2_empty_ftbl,
      "empty ftbl in a JP2 file: a box T.800 does not define, skipped (I.8): valid", CF_JP2, 0,
      X_VALID, NULL, NULL },
    { "siz.xsiz", rule_xsiz_profile_limit, "profile max+1 with one tile", 0, 0, X_PROF, "decode",
      NULL },
    { "siz.tile-covers-origin", rule_tile_short_of_origin,
      "XOsiz 2, XTOsiz 0, XTsiz 2: the first tile ends at the image origin", 0, 0, X_STD,
      "siz.tile-covers-origin", NULL },
    { "siz.mct-geometry", rule_mct_geometry, "MCT over three components, one sampled 2:1", 0, 0,
      X_STD, "siz.mct-geometry", NULL },
    { "sot.tpsot-below-tnsot", rule_tpsot_at_tnsot, "two tile-parts, both declaring TNsot 1", 0, 0,
      X_STD, "sot.tpsot-below-tnsot", NULL },
    { "tile.qcd-once", rule_tile_qcd_twice, "two QCD in the tile-part header", 0, 0, X_STD,
      "tile.qcd-once", NULL },
    { "tile.qcd-once", rule_tile_qcd_second_part, "QCD in the second tile-part", 0, 0, X_STD,
      "tile.qcd-once", NULL },
    { "file.ftyp-second", rule_ftyp_third, "an unknown box before ftyp (T.800 I.4)", 0, 0, X_STD,
      "file.ftyp-second", NULL },
    { "file.two-boxes", rule_signature_only, "the signature box alone (T.800 I.4)", 0, 0, X_STD,
      "file.two-boxes", NULL },
    { "coc.body", rule_coc_short, "Lcoc 5 (Table A.22: 9 to 43)", 0, 0, X_STD, "decode", NULL },
    { "poc.body", rule_poc_short, "Lpoc 5 (Table A.32: 9 to 65,535)", 0, 0, X_STD, "decode", NULL },
    { "coc.component", rule_coc_component, "Ccoc 1 for Csiz 1", 0, 0, X_STD, "coc.component",
      NULL },
    { "coc.once", rule_coc_twice, "two COC for component 0 in the main header (A.6.2)", 0, 0,
      X_STD, "coc.once", NULL },
    { "coc.precincts-count", rule_coc_precincts, "Scoc 1 without precinct sizes", 0, 0, X_STD,
      "coc.precincts-count", NULL },
    { "poc.resolution", rule_poc_resolution, "REpoc 1 for RSpoc 1 (Table A.32)", 0, 0, X_STD,
      "poc.resolution", NULL },
    { "poc.component", rule_poc_component, "CEpoc 1 for CSpoc 1 (Table A.32)", 0, 0, X_STD,
      "poc.component", NULL },
    { "poc.once", rule_poc_twice, "two POC in the main header (A.6.6)", 0, 0, X_STD, "poc.once",
      NULL },
    { "qcc", rule_qcc, "QCC for component 0: valid; the server skips it", 0, 0, X_VALID, NULL,
      NULL },
    { "qcc.component", rule_qcc_component, "Cqcc 1 for Csiz 1", 0, 0, X_LENIENT, "qcc.component",
      NULL },
    { "qcc.once", rule_qcc_twice, "two QCC for component 0 in the main header (A.6.5)", 0, 0,
      X_LENIENT, "qcc.once", NULL },
    { "qcc.length", rule_qcc_length, "no quantization in 2 SPqcc bytes (Equation A-5)", 0, 0,
      X_LENIENT, "qcc.length", NULL },
    { "qcd.style", rule_qcd_style, "Sqcd style 3 (Table A.28: reserved)", 0, 0, X_LENIENT,
      "qcd.style", NULL },
    { "qcd.length", rule_qcd_length, "no quantization in 2 SPqcd bytes (Equation A-4)", 0, 0,
      X_LENIENT, "qcd.length", NULL },
    { "qcd.reserved-bits", rule_qcd_reserved,
      "a reversible step size with a low bit set (Table A.29)", 0, 0, X_LENIENT,
      "qcd.reserved-bits", NULL },
    { "codestream.quantization-transform", rule_qcd_expounded,
      "scalar quantization with the 5-3 transform (Tables A.29, A.30; E.1.2.1)", 0, 0, X_LENIENT,
      "codestream.quantization-transform", NULL },
    { "rgn", rule_rgn, "RGN for component 0: valid; the server skips it", 0, 0, X_VALID, NULL,
      NULL },
    { "rgn.component", rule_rgn_component, "Crgn 1 for Csiz 1", 0, 0, X_LENIENT, "rgn.component",
      NULL },
    { "rgn.once", rule_rgn_twice, "two RGN for component 0 in the main header (A.6.3)", 0, 0,
      X_LENIENT, "rgn.once", NULL },
    { "rgn.srgn", rule_rgn_style, "Srgn 1 (Table A.25: reserved)", 0, 0, X_LENIENT, "decode",
      NULL },
    { "tlm", rule_tlm, "TLM of the one tile-part: valid; the server skips it", 0, 0, X_VALID, NULL,
      NULL },
    { "tlm.tile-parts", rule_tlm_length, "Ptlm one more than Psot (A.7.1)", 0, 0, X_LENIENT,
      "tlm.tile-parts", NULL },
    { "tlm.index", rule_tlm_index, "one TLM, with Ztlm 1", 0, 0, X_LENIENT, "tlm.index", NULL },
    { "tlm.stlm", rule_tlm_reserved, "Stlm bit 7 set (Table A.34)", 0, 0, X_LENIENT, "decode",
      NULL },
    { "plm", rule_plm, "PLM of the one tile-part: valid; the server skips it", 0, 0, X_VALID, NULL,
      NULL },
    { "plm.coverage", rule_plm_coverage, "PLM lengths one byte more than the data (A.7.2)", 0, 0,
      X_LENIENT, "plm.coverage", NULL },
    { "plm.tile-parts", rule_plm_tile_parts, "PLM runs for two tile-parts, one tile-part", 0, 0,
      X_LENIENT, "plm.tile-parts", NULL },
    { "plm.length", rule_plm_length, "Nplm one more than the lengths", 0, 0, X_LENIENT,
      "plm.length", NULL },
    { "ppm.packet-headers", rule_ppm_no_headers,
      "PPM with Nppm 0, the packet headers in the data (A.7.4)", 0, 0, X_STD,
      "ppm.packet-headers", NULL },
    { "ppm.tile-parts", rule_ppm_runs, "PPM runs for two tile-parts, one tile-part", 0, 0, X_STD,
      "ppm.tile-parts", NULL },
    { "ppm.ppt", rule_ppm_ppt, "PPM and PPT (A.7.4)", 0, 0, X_STD, "ppm.ppt", NULL },
    { "tile.packed-headers", rule_ppt, "PPT with the packet headers: profile invalid", 0, 0,
      X_PROF, "decode", NULL },
    { "ppt.index", rule_ppt_index, "two PPT with Zppt 0 in a header (A.7.5)", 0, 0, X_STD,
      "ppt.index", NULL },
    { "crg", rule_crg, "CRG: valid; the server skips it", 0, 0, X_VALID, NULL, NULL },
    { "crg.once", rule_crg_twice, "two CRG (A.9.1)", 0, 0, X_LENIENT, "crg.once", NULL },
    { "crg.length", rule_crg_length, "CRG of 2 bytes for Csiz 1 (Table A.42)", 0, 0, X_LENIENT,
      "decode", NULL },
    { "codestream.sop-unsignalled", rule_sop_unsignalled,
      "SOP in the data, not signalled in COD (A.8.1)", 0, 0, X_LENIENT,
      "codestream.sop-unsignalled", NULL },
    { "sop.length", rule_sop_length, "Lsop 5 (Table A.40: 4)", 0, 0, X_STD, "sop.length", NULL },
    { "codestream.eph-unsignalled", rule_eph_unsignalled,
      "EPH in the data, not signalled in COD (A.8.2)", 0, 0, X_LENIENT,
      "codestream.eph-unsignalled", NULL },
    { "codestream.eph-in-data", rule_eph_with_ppm, "EPH in the data with PPM (A.8.2)", 0, 0,
      X_STD, "codestream.eph-in-data", NULL },
    { "codestream.mct-transform", rule_mct_transform,
      "MCT over components 0 to 2, component 1 with the 9-7 transform (G.2, G.3)", 0, 0, X_STD,
      "codestream.mct-transform", NULL },
    { "codestream.profile-0", rule_profile0_cbstyle, "Rsiz 1 with selective bypass (Table A.45)",
      0, 0, X_LENIENT, "codestream.profile-0", NULL },
    { "codestream.profile-1", rule_profile1_codeblock, "Rsiz 2 with xcb 7 (Table A.45)", 0, 0,
      X_LENIENT, "codestream.profile-1", NULL },
    { "plt.zplt-index", rule_zplt_permuted,
      "Zplt 1 then 0: valid (A.7.3, A.1.3); the server takes them in order", 0, 1, X_PROF,
      "plt.zplt-sequence", NULL },
    { "file.ftyp-brand", rule_ftyp_brand,
      "ftyp brand 'abcd', 'jpx ' in the list: a JP2 file without 'jp2 ' (T.800 I.5.2)", CF_JPX, 0,
      X_STD, "file.ftyp-compatibility", "file.ftyp-brand" },
    { "coc.codeblock-area", rule_coc_codeblock_area,
      "COC code-block exponents 5 and 4: xcb + ycb 13 (A.6.1: at most 12)", 0, 0, X_STD,
      "coc.codeblock-area", NULL },
    { "coc.precincts-higher-zero", rule_coc_precincts_zero, "COC PPx 0 at r = 1 (Table A.21)",
      0, 1, X_STD, "coc.precincts-higher-zero", NULL },
    { "poc.first-tile-part", rule_poc_later_part,
      "POC in the second tile-part of a tile, none in the first (A.6.6)", 0, 0, X_STD,
      "poc.first-tile-part", NULL },
    { "qcc.style", rule_qcc_style, "Sqcc style 3 (Table A.28: reserved)", 0, 0, X_LENIENT,
      "qcc.style", NULL },
    { "qcc.reserved-bits", rule_qcc_reserved,
      "a reversible step size with a low bit set (Table A.29)", 0, 0, X_LENIENT,
      "qcc.reserved-bits", NULL },
    { "plm.index", rule_plm_index, "one PLM, with Zplm 1 (A.7.2)", 0, 0, X_LENIENT,
      "plm.index", NULL },
    { "plm.zero-length", rule_plm_zero, "PLM with a zero packet length, headers in the data",
      0, 0, X_LENIENT, "plm.zero-length", NULL },
    { "ppm.index", rule_ppm_index, "one PPM, with Zppm 1 (A.7.4)", 0, 0, X_STD, "ppm.index",
      NULL },
    { "ppm.length", rule_ppm_length, "Nppm one more than the packet headers (A.7.4)", 0, 0,
      X_STD, "ppm.length", NULL },
    { "codestream.profile-1", rule_profile1_ll_257,
      "Rsiz 2, Xsiz 257, NL 1: LL floor(257 / 2) = 128 (Table A.45): valid", 0, 1, X_VALID,
      NULL, NULL },
    { "codestream.profile-1", rule_profile1_ll_258,
      "Rsiz 2, Xsiz 258, NL 1: LL floor(258 / 2) = 129 (Table A.45: at most 128)", 0, 1,
      X_LENIENT, "codestream.profile-1", NULL },
};

/* Rule mutants specific to linked JPX (need the linked base). */
static FragmentList *flst_of(Jp2Family *f, int nth) {           /* the flst of ftbl nth */
    return &f->boxes.arr[box_index(f, TopPayload_ftbl_PRESENT, nth)].payload.u.ftbl
                .children.arr[0].payload.u.flst;
}
static DataReferences *dtbl_of(Jp2Family *f) {
    return &f->boxes.arr[box_index(f, TopPayload_dtbl_PRESENT, 0)].payload.u.dtbl;
}
static DataEntryUrl *url_of(Jp2Family *f, int nth) {
    return &dtbl_of(f)->references.arr[nth].payload.u.url;
}
static Bytes (*wire_patch)(Bytes valid);

/* Retypes every top-level `other` box ('abcd', which the encoder writes)
 * as a Media Data box. */
static Bytes mdat_patch(Bytes valid) {
    Bytes b = bytes_dup(valid);
    Regions rs = walk(&b);
    int i;
    for (i = 0; i < rs.n; ++i)
        if (rs.r[i].parent < 0 && strcmp(rs.r[i].kind, "LBox") == 0 &&
            be32(b.data + rs.r[i].start + 4) == MAPPING_OTHER_BOX)
            put32(b.data + rs.r[i].start + 4, HV_BOX_MDAT);
    free(rs.r);
    return b;
}

/* The codestream of ftbl k in this file (DR 0), for the first `n` ftbl:
 * a base codestream in a Media Data box at the end of the file (T.801
 * M.4: "the codestream shall be encapsulated within one or more Media Data
 * boxes"), the fragment its whole payload, from SOC (M.11.3.1). Encoding
 * the file once places the boxes; fixed-size fields keep them in place. */
static void add_local_codestreams(Jp2Family *f, int n) {
    Jp2Family *cs = xcalloc(sizeof *cs);
    Codestream *c;
    Bytes stream, placed;
    Regions rs;
    int k, i, found = 0;
    cs->boxes.nCount = 0;
    c = add_jp2c(cs, BASE_W, BASE_H, 0, 0, 1);
    stream = encode_family(cs, 0);                  /* one jp2c box */
    (void) c;
    for (k = 0; k < n; ++k) {
        TopBox *b = &f->boxes.arr[f->boxes.nCount++];
        b->payload.kind = TopPayload_other_PRESENT;
        OCTETS(b->payload.u.other.data, stream.data + HV_BOX_HEADER, stream.len - HV_BOX_HEADER);
    }
    placed = encode_family(f, 0);
    rs = walk(&placed);
    for (i = 0; i < rs.n && found < n; ++i) {
        const Region *r = &rs.r[i];
        Fragment *fr;
        if (r->parent >= 0 || strcmp(r->kind, "LBox") != 0 ||
            be32(placed.data + r->start + 4) != MAPPING_OTHER_BOX)
            continue;
        fr = &flst_of(f, found++)->fragments.arr[0];
        fr->off = r->payload_start;
        fr->len = (uint32_t) (r->end - r->payload_start);
        fr->dr = 0;
    }
    if (found != n) die("add_local_codestreams: boxes not placed");
    free(rs.r);
    free(placed.data);
    free(stream.data);
    free(cs);
    wire_patch = mdat_patch;
}

static void rule_dr_zero(Jp2Family *f, int box) {
    (void) box;
    add_local_codestreams(f, 1);
}
static void rule_dr_out_of_range(Jp2Family *f, int box) {
    (void) box;
    flst_of(f, 0)->fragments.arr[0].dr = 3;
}
static void rule_two_flst(Jp2Family *f, int box) {
    Superbox *sb = &f->boxes.arr[box_index(f, TopPayload_ftbl_PRESENT, 0)].payload.u.ftbl;
    (void) box;
    sb->children.arr[1] = sb->children.arr[0];
    sb->children.nCount = 2;
}
static void rule_ndr_mismatch(Jp2Family *f, int box) { (void) box; dtbl_of(f)->ndr = 3; }
static void rule_url_scheme(Jp2Family *f, int box) {
    (void) box;
    strcpy((char *) url_of(f, 0)->loc, "http://x/frame1.jp2");
}
static void rule_url_jpx_target(Jp2Family *f, int box) {
    (void) box;
    strcpy((char *) url_of(f, 0)->loc, "file://./frame1.jpx");
}
static void rule_url_version(Jp2Family *f, int box) { (void) box; url_of(f, 0)->header.vers = 1; }
/* A third codestream as jp2c beside the two ftbl ones, with its own jpch so
 * that the codestream count still matches and mixing is the only violation. */
static void rule_mixed_linked_embedded(Jp2Family *f, int box) {
    (void) box;
    add_jp2c(f, BASE_W, BASE_H, 0, 0, 1);
    add_jpch(f);
}
/* No dtbl, and every codestream in this file (DR 0), in Media Data boxes,
 * which the standard allows. The profile rejects it as jpx.linked-shape:
 * its count rules come before flst.dr-external, as in the reader. */
static void rule_no_dtbl(Jp2Family *f, int box) {
    int n = 0, i;
    (void) box;
    remove_box(f, box_index(f, TopPayload_dtbl_PRESENT, 0));
    for (i = 0; i < f->boxes.nCount; ++i) n += f->boxes.arr[i].payload.kind == TopPayload_ftbl_PRESENT;
    add_local_codestreams(f, n);
}
static void rule_two_dtbl(Jp2Family *f, int box) {
    (void) box;                                       /* second top-level dtbl */
    copy_box(f, box_index(f, TopPayload_dtbl_PRESENT, 0), f->boxes.nCount);
}
static void rule_fragment_early(Jp2Family *f, int box) {
    (void) box;
    flst_of(f, 0)->fragments.arr[0].off--;
}
static void rule_fragment_late(Jp2Family *f, int box) {
    (void) box;
    flst_of(f, 0)->fragments.arr[0].off++;
}
static void rule_fragment_short(Jp2Family *f, int box) {
    (void) box;
    flst_of(f, 0)->fragments.arr[0].len--;
}
static void rule_fragment_long(Jp2Family *f, int box) {
    (void) box;
    flst_of(f, 0)->fragments.arr[0].len++;
}
static void rule_jpxb_linked(Jp2Family *f, int box) {
    Ftyp *ftyp = ftyp_of(f);
    (void) box;
    ftyp->compat.arr[ftyp->compat.nCount++] = HV_BRAND_JPXB;
}
static void rule_fragment_outside_mdat(Jp2Family *f, int box) {
    Fragment *fr = &flst_of(f, 0)->fragments.arr[0];
    (void) box;
    fr->off = 12;                                   /* the ftyp box */
    fr->len = 20;
    fr->dr = 0;
}
static void rule_fragment_after_soc(Jp2Family *f, int box) {
    Fragment *fr = &flst_of(f, 0)->fragments.arr[0];
    (void) box;
    add_local_codestreams(f, 1);
    fr->off += 2;
    fr->len -= 2;
}
static void rule_url_empty(Jp2Family *f, int box) { (void) box; url_of(f, 0)->loc[0] = 0; }
static void rule_missing_companion(Jp2Family *f, int box) {
    (void) box;
    strcpy((char *) url_of(f, 0)->loc, "file://./jpx-missing-frame.jp2");
}

static void rule_flst_in_jpch(Jp2Family *f, int box) {
    Superbox *jpch = jpch_of(f);                           /* a copy of the first flst */
    (void) box;
    jpch->children.arr[jpch->children.nCount++] =
        f->boxes.arr[box_index(f, TopPayload_ftbl_PRESENT, 0)].payload.u.ftbl.children.arr[0];
}
static void rule_url_in_ftbl(Jp2Family *f, int box) {
    Superbox *ftbl = &f->boxes.arr[box_index(f, TopPayload_ftbl_PRESENT, 0)].payload.u.ftbl;
    (void) box;                                            /* a copy of the first url */
    ftbl->children.arr[ftbl->children.nCount++] = dtbl_of(f)->references.arr[0];
}
/* A byte after the NUL of the first url's LOC, inside its box. */
static void rule_url_terminator(Jp2Family *f, int box) {
    (void) box;
    OCTETS(url_of(f, 0)->extra, "\x00", 1);
}
static void set_xml(InnerBox *c);
/* An xml box in the dtbl beside the url boxes, NDR counting it. */
static void rule_dtbl_xml(Jp2Family *f, int box) {
    DataReferences *d = dtbl_of(f);
    (void) box;
    set_xml(&d->references.arr[d->references.nCount++]);
    d->ndr = d->references.nCount;
}
static void rule_url_bad_escape(Jp2Family *f, int box) {
    (void) box;
    strcpy((char *) url_of(f, 0)->loc, "file://./jpx-linked-frame1%zz.jp2");
}
/* NF 2 in the first flst, which holds one fragment. */
static void rule_flst_nf(Jp2Family *f, int box) { (void) box; flst_of(f, 0)->nf = 2; }
/* The first flst at the edges of T.801 Table M.17: OFF 11 (below its 12),
 * LEN 0, and NF 0 with no fragment. */
static void rule_flst_off_low(Jp2Family *f, int box) {
    (void) box;
    flst_of(f, 0)->fragments.arr[0].off = 11;
}
static void rule_flst_len_zero(Jp2Family *f, int box) {
    (void) box;
    flst_of(f, 0)->fragments.arr[0].len = 0;
}
static void rule_flst_no_fragment(Jp2Family *f, int box) {
    (void) box;
    flst_of(f, 0)->nf = 0;
    flst_of(f, 0)->fragments.nCount = 0;
}
/* A mutant whose violation no field of the structs can hold sets this; the
 * driver applies it to the encoded file. */
static Bytes flst_trailing_byte(Bytes valid);
/* A byte after the only fragment of the first flst, inside the box: at
 * both layers the list, `size deduced`, fails on a partial fragment. */
static void rule_flst_trailing_byte(Jp2Family *f, int box) {
    (void) f;
    (void) box;
    wire_patch = flst_trailing_byte;
}

/* Appended entries only: array positions are in fixture names. */
static const RuleMutant linked_rule_mutants[] = {
    { "jpx.reader-requirements", rule_missing_rreq,
      "no rreq box: standard invalid, profile accepted", CF_JPX, 0, X_LENIENT,
      "jpx.reader-requirements", NULL },
    { "file.unknown-box", rule_unknown_box, "unknown top-level box: valid and skipped", CF_JPX, 0,
      X_VALID, NULL, NULL },
    { "flst.dr-external", rule_dr_zero,
      "DR 0: the first codestream in this file, in an mdat: profile invalid", CF_JPX, 0, X_PROF,
      "flst.dr-external", NULL },
    { "flst.dr-range", rule_dr_out_of_range, "DR = ndr+1", CF_JPX, 0, X_STD, "flst.dr-range",
      NULL },
    { "ftbl.one-flst", rule_two_flst, "two flst in one ftbl", CF_JPX, 0, X_STD, "ftbl.one-flst",
      NULL },
    { "dtbl.ndr-count", rule_ndr_mismatch, "NDR = 3 with 2 url boxes", CF_JPX, 0, X_STD,
      "dtbl.ndr-count", NULL },
    { "url.file-scheme", rule_url_scheme, "http URL: profile invalid", CF_JPX, 0, X_PROF,
      "url.file-scheme", NULL },
    { "url.version-flags", rule_url_version, "VERS = 1 (T.800 I.7.3.2: 0)", CF_JPX, 0, X_STD,
      "decode", "decode" },
    { "url.jp2-target", rule_url_jpx_target, "link to a .jpx: profile invalid", CF_JPX, 0, X_PROF,
      "url.jp2-target", NULL },
    { "jpx.mixed-sources", rule_mixed_linked_embedded, "jp2c next to ftbl: profile invalid", CF_JPX,
      0, X_PROF, "jpx.mixed-sources", NULL },
    { "jpx.linked-shape", rule_no_dtbl,
      "no dtbl, the codestreams in this file (DR 0), in mdat boxes: profile invalid", CF_JPX, 0,
      X_PROF, "jpx.linked-shape", NULL },
    { "jpx.one-dtbl", rule_two_dtbl, "two dtbl boxes (T.801 M.11.2)", CF_JPX, 0, X_STD,
      "jpx.one-dtbl", NULL },
    { "flst.source-extent", rule_fragment_early,
      "fragment starts one byte before companion codestream", CF_JPX, 0, X_PROF,
      "flst.source-extent", NULL },
    { "flst.source-extent", rule_fragment_late,
      "fragment starts one byte after companion codestream", CF_JPX, 0, X_PROF,
      "flst.source-extent", NULL },
    { "flst.source-extent", rule_fragment_short, "fragment omits final companion codestream byte",
      CF_JPX, 0, X_PROF, "flst.source-extent", NULL },
    { "flst.source-extent", rule_fragment_long, "fragment extends past companion codestream",
      CF_JPX, 0, X_PROF, "flst.source-extent", NULL },
    { "url.missing-companion", rule_missing_companion,
      "structurally valid JPX with unavailable companion", CF_JPX, 0, X_PROF,
      "url.missing-companion", NULL },
    { "box.flst-placement", rule_flst_in_jpch, "flst inside a jpch (T.801 M.11.3: in ftbl)", CF_JPX,
      0, X_STD, "box.flst-placement", NULL },
    { "box.url-placement", rule_url_in_ftbl, "url inside an ftbl (T.801 M.11.2: in dtbl)", CF_JPX,
      0, X_STD, "box.url-placement", NULL },
    { "url.terminator", rule_url_terminator, "a byte after LOC's NUL, inside the url box", CF_JPX,
      0, X_STD, "box.extent", "url.terminator" },
    { "flst.nf-count", rule_flst_nf, "NF 2 with one fragment (T.801 M.11.3.1)", CF_JPX, 0, X_STD,
      "flst.nf-count", "decode" },
    { "flst.one-fragment", rule_flst_trailing_byte,
      "a byte after the only fragment, inside the flst box (T.801 M.11.3.1)", CF_JPX, 0, X_STD,
      "decode", "decode" },
    { "flst.off", rule_flst_off_low, "OFF 11 (Table M.17: 12 or more)", CF_JPX, 0, X_STD, "decode",
      "decode" },
    { "flst.source-extent", rule_flst_len_zero, "LEN 0 (Table M.17: valid): profile invalid",
      CF_JPX, 0, X_PROF, "flst.source-extent", NULL },
    { "flst.one-fragment", rule_flst_no_fragment, "NF 0, no fragment (Table M.17: valid)", CF_JPX,
      0, X_PROF, "decode", NULL },
    { "dtbl.non-url", rule_dtbl_xml, "an xml box in the dtbl (T.801 M.11.2: url boxes)", CF_JPX, 0,
      X_STD, "dtbl.non-url", NULL },
    { "url.percent-encoding", rule_url_bad_escape, "LOC with the escape %zz: profile invalid",
      CF_JPX, 0, X_PROF, "url.percent-encoding", NULL },
    { "jpxb.fragments", rule_jpxb_linked,
      "'jpxb', the first codestream in another file (T.801 M.9.2.5)", CF_JPX, 0, X_LENIENT,
      "jpxb.fragments", NULL },
    { "flst.mdat", rule_fragment_outside_mdat,
      "DR 0, the fragment at this file's ftyp (T.801 M.4: in an mdat)", CF_JPX, 0, X_STD,
      "flst.mdat", "flst.dr-external" },
    { "flst.codestream-start", rule_fragment_after_soc,
      "DR 0, the fragment in an mdat after SOC (T.801 M.11.3.1)", CF_JPX, 0, X_STD,
      "flst.codestream-start", "flst.dr-external" },
    { "url.loc", rule_url_empty, "LOC empty (T.800 I.7.3.2): valid, profile invalid", CF_JPX, 0,
      X_PROF, "decode", NULL },
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
    const SizFixed *z = &s->fixed;
    int i;
    if (c->payload.kind != InnerPayload_ihdr_PRESENT ||
        z->ysiz <= z->yosiz || z->xsiz <= z->xosiz || z->ysiz - z->yosiz > 4294967295u ||
        z->xsiz - z->xosiz > 4294967295u || z->csiz < 1 || z->csiz > 16384 ||
        s->components.nCount < 1)
        return;
    for (i = 0; i < s->components.nCount; ++i)
        if (s->components.arr[i].depthMinus1 > 37 ||
            s->components.arr[i].depthMinus1 != s->components.arr[0].depthMinus1 ||
            s->components.arr[i].isSigned != s->components.arr[0].isSigned)
            return;
    ihdr->height = z->ysiz - z->yosiz;
    ihdr->width = z->xsiz - z->xosiz;
    ihdr->nc = z->csiz;
    ihdr->bpc = s->components.arr[0].depthMinus1 | (s->components.arr[0].isSigned ? 128 : 0);
}

static void sync_headers(Jp2Family *f) {
    Superbox *header = NULL;
    int i, k, stream = 0;
    for (i = 0; i < f->boxes.nCount; ++i) {
        TopPayload *p = &f->boxes.arr[i].payload;
        if (p->kind == TopPayload_jp2h_PRESENT && header == NULL) header = &p->u.jp2h;
        if (p->kind != TopPayload_jp2c_PRESENT) continue;
        if (stream++ == 0 && header != NULL && header->children.nCount > 0) {
            int s;
            sync_ihdr(&header->children.arr[0], &p->u.jp2c.siz.body);
            /* T.800 I.5.3.6: with the multiple component transform, an
             * RGB colourspace: sRGB for the first colr. */
            for (s = 0; s < p->u.jp2c.segments.nCount; ++s)
                if (p->u.jp2c.segments.arr[s].code == HV_COD) {
                    if (p->u.jp2c.segments.arr[s].cod.body.sgcod.mct == 1 &&
                        header->children.nCount > 1 &&
                        header->children.arr[1].payload.kind == InnerPayload_colr_PRESENT)
                        header->children.arr[1].payload.u.colr.header.enumcs = 16;
                    break;
                }
        }
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
 * jp2h jpch jpch jp2c jp2c, each jpch holding an ihdr (jpch_of above). */
static Superbox *jp2h_of(Jp2Family *f) {
    return &f->boxes.arr[box_index(f, TopPayload_jp2h_PRESENT, 0)].payload.u.jp2h;
}

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

/* A Resolution box: resc (72 dpi, 2835 grid points per meter, as 2835/1)
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

/* JP2 mutants: jp2h holds ihdr and a greyscale colr. */
static void hdr_palette(Jp2Family *f, int box) {
    (void) box;
    set_pclr(append_child(jp2h_of(f)));
    set_cmap(append_child(jp2h_of(f)), 0, 1, 0);
}
static void hdr_resolution(Jp2Family *f, int box) { (void) box; set_res(append_child(jp2h_of(f)), 1, 1, 0); }
static void hdr_other_child(Jp2Family *f, int box) { (void) box; set_xml(append_child(jp2h_of(f))); }
static void hdr_second_colr(Jp2Family *f, int box) {
    (void) box;
    set_colr(f, append_child(jp2h_of(f)), 1, 17);
}
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
    remove_box(f, box_index(f, TopPayload_jp2h_PRESENT, 0));
}
static void hdr_two_jp2h(Jp2Family *f, int box) {
    int jp2h = box_index(f, TopPayload_jp2h_PRESENT, 0);
    (void) box;
    copy_box(f, jp2h, jp2h + 1);
}
static void hdr_jp2h_after_jp2c(Jp2Family *f, int box) {
    (void) box;
    move_box(f, box_index(f, TopPayload_jp2h_PRESENT, 0),
             box_index(f, TopPayload_jp2c_PRESENT, 0));
}
static void hdr_no_jp2c(Jp2Family *f, int box) {
    (void) box;
    remove_box(f, box_index(f, TopPayload_jp2c_PRESENT, 0));
}
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
    set_colr(f, append_child(jp2h_of(f)), 1, 16);
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
static void hdr_colr_method(Jp2Family *f, int box) {
    (void) box;
    set_colr(f, &jp2h_of(f)->children.arr[1], 3, 0);
}
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
static void hdr_jpx_jplh_cgrp(Jp2Family *f, int colrs, int approx, int other);
/* Without jp2h the colours are a jplh's (T.801 M.11.7.1). */
static void hdr_jpx_no_ihdr(Jp2Family *f, int box) {
    (void) box;
    jpch_of(f)->children.nCount = 0;
    remove_box(f, box_index(f, TopPayload_jp2h_PRESENT, 0));
    hdr_jpx_jplh_cgrp(f, 1, 1, 0);
}
static void hdr_jpx_defaults(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[box_index(f, TopPayload_jpch_PRESENT, 0)].payload.u.jpch.children.nCount = 0;
    f->boxes.arr[box_index(f, TopPayload_jpch_PRESENT, 1)].payload.u.jpch.children.nCount = 0;
}
static void hdr_jpx_no_jp2h(Jp2Family *f, int box) {
    (void) box;
    remove_box(f, box_index(f, TopPayload_jp2h_PRESENT, 0));
    hdr_jpx_jplh_cgrp(f, 1, 1, 0);
}
static void hdr_jpx_late_jp2h(Jp2Family *f, int box) {
    (void) box;
    remove_box(f, box_index(f, TopPayload_jp2h_PRESENT, 0));
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
    set_colr(f, append_child(jp2h_of(f)), 1, 16);
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

static void hdr_rreq_mask_length(Jp2Family *f, int box, int ml) {
    Rreq *r = &f->boxes.arr[box_index(f, TopPayload_rreq_PRESENT, 0)].payload.u.rreq;
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
    set_cdef(append_child(&f->boxes.arr[box_index(f, TopPayload_jplh_PRESENT, 0)].payload.u.jplh),
             2, 0, 0, 1);
}
/* Boxes placed in jp2h that belong elsewhere (layer 1 only: the profile
 * keeps jp2h opaque, as the server does). */
static void hdr_jp2c_in_jp2h(Jp2Family *f, int box) {
    InnerBox *c = append_child(jp2h_of(f));
    (void) box;
    c->payload.kind = InnerPayload_jp2c_PRESENT;
    OCTETS(c->payload.u.jp2c.data, "\x00", 1);
}
static void hdr_flst_in_jp2h(Jp2Family *f, int box) {
    InnerBox *c = append_child(jp2h_of(f));
    FragmentList *flst = &c->payload.u.flst;
    (void) box;
    c->payload.kind = InnerPayload_flst_PRESENT;                 /* one fragment, this file */
    flst->nf = 1;
    flst->fragments.nCount = 1;
    flst->fragments.arr[0].off = 12;
    flst->fragments.arr[0].len = 1;
    flst->fragments.arr[0].dr = 0;
}

/* JPX colr in jp2h, METH 1 (T.801 M.11.7.2): EnumCS and `ep` EP bytes. */
static void set_jpx_colr_ep(Jp2Family *f, uint32_t enumcs, int ep) {
    static const unsigned char zero[28];
    InnerBox *c = &jp2h_of(f)->children.arr[1];
    c->payload.u.colr.header.enumcs = enumcs;
    OCTETS(c->payload.u.colr.rest, zero, ep);
}
static void hdr_jpx_cielab_ep(Jp2Family *f, int box) { (void) box; set_jpx_colr_ep(f, 14, 28); }
static void hdr_jpx_ciejab_ep(Jp2Family *f, int box) { (void) box; set_jpx_colr_ep(f, 19, 24); }
static void hdr_jpx_cielab_short_ep(Jp2Family *f, int box) { (void) box; set_jpx_colr_ep(f, 14, 4); }
static void hdr_jpx_approx_zero(Jp2Family *f, int box) {
    (void) box;
    jp2h_of(f)->children.arr[1].payload.u.colr.header.approx = 0;
}
static void hdr_jpx_approx_five(Jp2Family *f, int box) {
    (void) box;
    jp2h_of(f)->children.arr[1].payload.u.colr.header.approx = 5;
}
static void hdr_jpx_minor_zero(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[1].payload.u.ftyp.header.minor = 0;
}
/* An IPR box (T.800 I.6; contents reserved): top-level, or in a header box. */
static void add_top_ipr(Jp2Family *f) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->payload.kind = TopPayload_jp2i_PRESENT;
    b->payload.u.jp2i.data.nCount = 0;
}
static void set_ipr_box(InnerBox *c) {
    c->payload.kind = InnerPayload_jp2i_PRESENT;
    c->payload.u.jp2i.data.nCount = 0;
}
static void hdr_ipr_flag_no_box(Jp2Family *f, int box) {
    (void) box;
    jp2h_of(f)->children.arr[0].payload.u.ihdr.ipr = 1;
}
static void hdr_ipr_box_no_flag(Jp2Family *f, int box) { (void) box; add_top_ipr(f); }
static void hdr_ipr_flag_and_box(Jp2Family *f, int box) {
    hdr_ipr_flag_no_box(f, box);
    add_top_ipr(f);
}
static void hdr_jpx_jpch_ipr_no_flag(Jp2Family *f, int box) {
    (void) box;
    set_ipr_box(append_child(jpch_of(f)));
}
static void hdr_jpx_jpch_ipr_flag(Jp2Family *f, int box) {
    (void) box;
    jpch_of(f)->children.arr[0].payload.u.ihdr.ipr = 1;
    set_ipr_box(append_child(jpch_of(f)));
}
static void hdr_jpx_jpch_ipr_flag_no_box(Jp2Family *f, int box) {
    (void) box;
    jpch_of(f)->children.arr[0].payload.u.ihdr.ipr = 1;
}
static void hdr_jpx_jpch_ipr_flag_top_box(Jp2Family *f, int box) {
    hdr_jpx_jpch_ipr_flag_no_box(f, box);
    add_top_ipr(f);
}
/* Codestream Registration (T.801 M.11.7.7): XS, YS 1, then CDN 0, XR, YR
 * 1, XO, YO 0. */
static void set_creg(InnerBox *c) {
    c->payload.kind = InnerPayload_creg_PRESENT;
    OCTETS(c->payload.u.creg.data, "\x00\x01\x00\x01\x00\x00\x01\x01\x00\x00", 10);
}
/* Two jplh after the codestreams, the first with a creg; `both`: the
 * second too. */
static void add_jplh_creg(Jp2Family *f, int both) {
    int i;
    for (i = 0; i < 2; ++i) {
        TopBox *b = &f->boxes.arr[f->boxes.nCount++];
        b->payload.kind = TopPayload_jplh_PRESENT;
        b->payload.u.jplh.children.nCount = 0;
        if (i == 0 || both) set_creg(append_child(&b->payload.u.jplh));
    }
}
static void hdr_jpx_creg_one(Jp2Family *f, int box) { (void) box; add_jplh_creg(f, 0); }
static void hdr_empty_jp2h(Jp2Family *f, int box) { (void) box; jp2h_of(f)->children.nCount = 0; }
static void hdr_jpx_bpc_255_no_bpcc(Jp2Family *f, int box) {
    (void) box;
    jpch_of(f)->children.arr[0].payload.u.ihdr.bpc = 255;
}
/* A Colour Group box (T.801 M.11.7.1): `colrs` greyscale colr boxes with
 * APPROX `approx`, then an unknown box when `other` (T.800 I.8: skipped). */
static void set_cgrp(InnerBox *c, int colrs, int approx, int other) {
    Cgrp *g = &c->payload.u.cgrp;
    int n = 0;
    c->payload.kind = InnerPayload_cgrp_PRESENT;
    for (; n < colrs; ++n) {
        CgrpPayload *p = &g->children.arr[n].payload;
        p->kind = CgrpPayload_colr_PRESENT;
        p->u.colr.header.meth = 1;
        p->u.colr.header.prec = 0;
        p->u.colr.header.approx = approx;
        p->u.colr.header.exist.enumcs = 1;
        p->u.colr.header.enumcs = 17;
        p->u.colr.rest.nCount = 0;
    }
    if (other) {
        g->children.arr[n].payload.kind = CgrpPayload_other_PRESENT;
        OCTETS(g->children.arr[n].payload.u.other.data, "\x01", 1);
        n++;
    }
    g->children.nCount = n;
}
/* A jplh after the codestreams holding a cgrp. */
static void hdr_jpx_jplh_cgrp(Jp2Family *f, int colrs, int approx, int other) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->payload.kind = TopPayload_jplh_PRESENT;
    b->payload.u.jplh.children.nCount = 0;
    set_cgrp(append_child(&b->payload.u.jplh), colrs, approx, other);
}
static void hdr_jpx_cgrp(Jp2Family *f, int box) { (void) box; hdr_jpx_jplh_cgrp(f, 1, 1, 1); }
static void hdr_jpx_cgrp_approx_zero(Jp2Family *f, int box) { (void) box; hdr_jpx_jplh_cgrp(f, 1, 0, 0); }
static void hdr_jpx_cgrp_two_enumerated(Jp2Family *f, int box) { (void) box; hdr_jpx_jplh_cgrp(f, 2, 1, 0); }
static void hdr_jpx_cgrp_no_colr(Jp2Family *f, int box) { (void) box; hdr_jpx_jplh_cgrp(f, 0, 1, 1); }
static void hdr_jpx_cgrp_in_jpch(Jp2Family *f, int box) { (void) box; set_cgrp(append_child(jpch_of(f)), 1, 1, 0); }
static void hdr_jpx_cgrp_top(Jp2Family *f, int box) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];         /* after the codestreams */
    (void) box;
    b->payload.kind = TopPayload_cgrp_PRESENT;
    OCTETS(b->payload.u.cgrp.data, "\x00\x00\x00\x0F""colr\x01\x00\x01\x00\x00\x00\x11", 15);
}
/* A greyscale colr box at the top level, after the codestreams. */
static void hdr_top_colr(Jp2Family *f, int box) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    (void) box;
    b->payload.kind = TopPayload_colr_PRESENT;
    OCTETS(b->payload.u.colr.data, is_jpx(f) ? "\x01\x00\x01\x00\x00\x00\x11"
                                             : "\x01\x00\x00\x00\x00\x00\x11", 7);
}
static void hdr_cgrp_in_jp2h(Jp2Family *f, int box) { (void) box; set_cgrp(append_child(jp2h_of(f)), 1, 0, 0); }
static void hdr_jpx_two_jp2h(Jp2Family *f, int box) {
    int at = box_index(f, TopPayload_jp2h_PRESENT, 0);
    (void) box;
    copy_box(f, at, at + 1);                                  /* two jp2h, adjacent */
}
static void hdr_jpx_creg_both(Jp2Family *f, int box) { (void) box; add_jplh_creg(f, 1); }

/* Appended entries only: array positions are in fixture names. */

/* ------------------------------------------------------------------------ */
/* Boxes the whole-file model keeps opaque, on the wire                      */
/* ------------------------------------------------------------------------ */

/* The encoder writes MAPPING_OTHER_BOX ('abcd') for an `other` box: a
 * mutant that adds one queues the type the box is to have, in file
 * order, and retype_patch writes them into the encoded file. */
static uint32_t retype_queue[8];
static int retypes;

static Bytes retype_patch(Bytes valid) {
    Bytes b = bytes_dup(valid);
    Regions rs = walk(&b);
    int i, k = 0;
    for (i = 0; i < rs.n; ++i)
        if (strcmp(rs.r[i].kind, "LBox") == 0 &&
            be32(b.data + rs.r[i].start + 4) == MAPPING_OTHER_BOX) {
            if (k == retypes) die("retype_patch: more `other` boxes than queued types");
            put32(b.data + rs.r[i].start + 4, retype_queue[k++]);
        }
    free(rs.r);
    if (k != retypes) die("retype_patch: fewer `other` boxes than queued types");
    return b;
}

static void queue_type(const char *type) {
    if (retypes == (int) (sizeof retype_queue / sizeof *retype_queue)) die("retype queue full");
    retype_queue[retypes++] = be32((const unsigned char *) type);
    wire_patch = retype_patch;
}

/* A box of `type` with the n bytes of `data`, as the last box of the file
 * or the last child of sb, opaque to the model. */
static void other_top(Jp2Family *f, const char *type, const void *data, size_t n) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->payload.kind = TopPayload_other_PRESENT;
    OCTETS(b->payload.u.other.data, data, n);
    queue_type(type);
}
static void other_child(Superbox *sb, const char *type, const void *data, size_t n) {
    InnerBox *c = append_child(sb);
    c->payload.kind = InnerPayload_other_PRESENT;
    OCTETS(c->payload.u.other.data, data, n);
    queue_type(type);
}

/* resd and a colr in res, which T.800 Figure I.1 puts in jp2h. */
static void hdr_res_colr(Jp2Family *f, int box) {
    (void) box;
    set_res(append_child(jp2h_of(f)), 0, 1, 1);
    queue_type("colr");
}
/* A box on the wire into out: LBox, TBox, then the n bytes of data. Its
 * length. */
static size_t put_box(unsigned char *out, const char *type, const void *data, size_t n) {
    put32(out, (uint32_t) (HV_BOX_HEADER + n));
    memcpy(out + 4, type, 4);
    memcpy(out + HV_BOX_HEADER, data, n);
    return HV_BOX_HEADER + n;
}

/* A UUID Info box (T.800 I.7.3): a ulst of `nu` in NU and `ids` UUIDs, and
 * a url with VERS `vers` and the `loc` bytes (its NUL included), as far as
 * each is asked for. */
static size_t uinf_payload(unsigned char *out, int ulst, int nu, int ids, int url, int vers,
                           const char *loc, size_t loc_n) {
    unsigned char list[2 + 2 * 16], entry[4 + 64];
    size_t n = 0;
    if (ulst) {
        put16(list, (uint16_t) nu);
        memset(list + 2, 0x11, (size_t) ids * 16);
        n += put_box(out + n, "ulst", list, 2 + (size_t) ids * 16);
    }
    if (url) {
        put32(entry, (uint32_t) vers << 24);
        memcpy(entry + 4, loc, loc_n);
        n += put_box(out + n, "url ", entry, 4 + loc_n);
    }
    return n;
}

static void add_uinf(Jp2Family *f, Superbox *parent, int ulst, int nu, int ids, int url,
                     int vers, const char *loc, size_t loc_n) {
    unsigned char payload[256];
    size_t n = uinf_payload(payload, ulst, nu, ids, url, vers, loc, loc_n);
    if (parent) other_child(parent, "uinf", payload, n);
    else other_top(f, "uinf", payload, n);
}
static void hdr_uinf(Jp2Family *f, int box) {
    (void) box;
    add_uinf(f, NULL, 1, 1, 1, 1, 0, "uuids.xml", 10);
}
static void hdr_uinf_in_jp2h(Jp2Family *f, int box) {
    (void) box;
    add_uinf(f, jp2h_of(f), 1, 1, 1, 1, 0, "uuids.xml", 10);
}
static void hdr_uinf_url_only(Jp2Family *f, int box) {
    (void) box;
    add_uinf(f, NULL, 0, 0, 0, 1, 0, "uuids.xml", 10);
}
static void hdr_uinf_nu(Jp2Family *f, int box) {
    (void) box;
    add_uinf(f, NULL, 1, 2, 1, 1, 0, "uuids.xml", 10);
}
static void hdr_uinf_vers(Jp2Family *f, int box) {
    (void) box;
    add_uinf(f, NULL, 1, 1, 1, 1, 1, "uuids.xml", 10);
}
static void hdr_uinf_no_nul(Jp2Family *f, int box) {
    (void) box;
    add_uinf(f, NULL, 1, 1, 1, 1, 0, "uuids.xml", 9);
}
static void hdr_uinf_after_nul(Jp2Family *f, int box) {
    (void) box;
    add_uinf(f, NULL, 1, 1, 1, 1, 0, "uuids.xml\0x", 11);
}
static void hdr_uinf_empty_loc(Jp2Family *f, int box) {
    (void) box;
    add_uinf(f, NULL, 1, 1, 1, 1, 0, "", 1);
}
static void hdr_uuid(Jp2Family *f, int box) {
    static const unsigned char uuid[20] = { 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                            0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                            'd', 'a', 't', 'a' };
    (void) box;
    other_top(f, "uuid", uuid, sizeof uuid);
}
static void hdr_uuid_short(Jp2Family *f, int box) {
    (void) box;
    other_top(f, "uuid", "\x22\x22\x22\x22", 4);
}

static void hdr_second_signature(Jp2Family *f, int box) {
    (void) box;
    add_opaque_box(f, HV_BOX_JP, "\x0D\x0A\x87\x0A", 4);
}
static void hdr_second_ftyp(Jp2Family *f, int box) {
    (void) box;
    copy_box(f, box_index(f, TopPayload_ftyp_PRESENT, 0), f->boxes.nCount);
}
static void hdr_jp2_minor_one(Jp2Family *f, int box) {
    (void) box;
    ftyp_of(f)->header.minor = 1;
}
static void hdr_top_ihdr(Jp2Family *f, int box) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    (void) box;
    b->payload.kind = TopPayload_ihdr_PRESENT;
    OCTETS(b->payload.u.ihdr.data, "\x00\x00\x00\x04\x00\x00\x00\x04\x00\x01\x07\x07\x00\x00", 14);
}
static void hdr_second_colr_meth3(Jp2Family *f, int box) {
    InnerBox *c;
    (void) box;
    c = insert_child(jp2h_of(f), 2);
    set_colr(f, c, 3, 0);
    OCTETS(c->payload.u.colr.rest, "\x00\x00\x00\x00", 4);
}

/* A restricted ICC profile (T.800 I.5.3.3): the Monochrome Input class
 * ('scnr', 'GRAY', PCS 'XYZ ') with the four tags it requires, each tag's
 * data 12 bytes; `fault` spoils one part of it. Its length. */
enum { ICC_OK, ICC_SIZE, ICC_CLASS, ICC_PCS, ICC_TAGS };
static size_t icc_profile(unsigned char *p, int fault) {
    static const char tags[4][5] = { "desc", "kTRC", "wtpt", "cprt" };
    int count = fault == ICC_TAGS ? 3 : 4, i;          /* ICC_TAGS: no cprt */
    size_t n = 128 + 4 + (size_t) count * 12 + (size_t) count * 12;
    memset(p, 0, n);
    put32(p, (uint32_t) (fault == ICC_SIZE ? n + 1 : n));
    memcpy(p + 12, fault == ICC_CLASS ? "mntr" : "scnr", 4);
    memcpy(p + 16, "GRAY", 4);
    memcpy(p + 20, fault == ICC_PCS ? "Lab " : "XYZ ", 4);
    memcpy(p + 36, "acsp", 4);
    put32(p + 128, (uint32_t) count);
    for (i = 0; i < count; ++i) {
        unsigned char *e = p + 132 + 12 * i;
        memcpy(e, tags[i], 4);
        put32(e + 4, (uint32_t) (132 + 12 * count + 12 * i));
        put32(e + 8, 12);
    }
    return n;
}
static void hdr_icc(Jp2Family *f, int fault) {
    InnerBox *c = &jp2h_of(f)->children.arr[1];
    unsigned char profile[256];
    size_t n = icc_profile(profile, fault);
    set_colr(f, c, 2, 0);
    OCTETS(c->payload.u.colr.rest, profile, n);
}
static void hdr_icc_ok(Jp2Family *f, int box) { (void) box; hdr_icc(f, ICC_OK); }
static void hdr_icc_size(Jp2Family *f, int box) { (void) box; hdr_icc(f, ICC_SIZE); }
static void hdr_icc_class(Jp2Family *f, int box) { (void) box; hdr_icc(f, ICC_CLASS); }
static void hdr_icc_pcs(Jp2Family *f, int box) { (void) box; hdr_icc(f, ICC_PCS); }
static void hdr_icc_tags(Jp2Family *f, int box) { (void) box; hdr_icc(f, ICC_TAGS); }

/* A palette of 7-bit values, one of which sets the padding bit. */
static void hdr_pclr_padding(Jp2Family *f, int box) {
    InnerBox *c;
    (void) box;
    hdr_palette(f, box);
    c = &jp2h_of(f)->children.arr[2];
    c->payload.u.pclr.header.depths.arr[0] = 6;
    c->payload.u.pclr.entries.arr[1] = 0xFF;
}

/* Channels from a palette of `columns` columns, described by cdef as
 * {Cn, Typ, Asoc} triples. */
static void palette_channels(Jp2Family *f, int columns, const int (*d)[3], int n) {
    InnerBox *c;
    int i;
    set_pclr_columns(append_child(jp2h_of(f)), columns);
    set_cmap_columns(append_child(jp2h_of(f)), columns);
    c = append_child(jp2h_of(f));
    set_cdef(c, n, 0, 0, 0);
    for (i = 0; i < n; ++i) {
        c->payload.u.cdef.entries.arr[i].cn = d[i][0];
        c->payload.u.cdef.entries.arr[i].typ = d[i][1];
        c->payload.u.cdef.entries.arr[i].asoc = d[i][2];
    }
}
static void hdr_cdef_two_opacities(Jp2Family *f, int box) {
    static const int d[3][3] = { { 0, 0, 1 }, { 1, 1, 0 }, { 2, 1, 1 } };
    (void) box;
    palette_channels(f, 3, d, 3);
}
static void hdr_cdef_signed_opacity(Jp2Family *f, int box) {
    static const int d[2][3] = { { 0, 0, 1 }, { 1, 1, 0 } };
    (void) box;
    palette_channels(f, 2, d, 2);
    jp2h_of(f)->children.arr[2].payload.u.pclr.header.depths.arr[1] = 0x87;
}
static void hdr_cdef_two_channels_one_pair(Jp2Family *f, int box) {
    static const int d[2][3] = { { 0, 0, 1 }, { 1, 0, 1 } };
    (void) box;
    palette_channels(f, 2, d, 2);
}
static void hdr_cdef_asoc(Jp2Family *f, int box) { (void) box; set_cdef(append_child(jp2h_of(f)), 1, 0, 0, 2); }

/* Three components through the multiple component transform (RCT, G.2),
 * the image header in step. */
static void three_components(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    TilePart *tp = tp_of(f, box);
    int i;
    cs->siz.body.fixed.csiz = 3;
    cs->siz.body.components.nCount = 3;
    cs->siz.body.components.arr[1] = cs->siz.body.components.arr[0];
    cs->siz.body.components.arr[2] = cs->siz.body.components.arr[0];
    cod_of(f, box)->sgcod.mct = 1;
    for (i = 1; i < 3 * tp->rest.data.nCount; ++i)
        tp->rest.headers.arr[0].plt.body.entries.arr[i] = tp->rest.headers.arr[0].plt.body.entries.arr[0];
    tp->rest.headers.arr[0].plt.body.entries.nCount *= 3;
    tp->rest.data.nCount *= 3;
    jp2h_of(f)->children.arr[0].payload.u.ihdr.nc = 3;
}
static void hdr_mct_srgb(Jp2Family *f, int box) {
    three_components(f, box);
    jp2h_of(f)->children.arr[1].payload.u.colr.header.enumcs = 16;
}
static void hdr_mct_grey(Jp2Family *f, int box) { three_components(f, box); }
static void hdr_mct_channels(Jp2Family *f, int box) {
    InnerBox *c;
    hdr_mct_srgb(f, box);
    c = append_child(jp2h_of(f));
    set_cdef(c, 3, 0, 0, 0);
    c->payload.u.cdef.entries.arr[0].asoc = 2;
    c->payload.u.cdef.entries.arr[1].cn = 1;
    c->payload.u.cdef.entries.arr[1].asoc = 1;
    c->payload.u.cdef.entries.arr[2].cn = 2;
    c->payload.u.cdef.entries.arr[2].asoc = 3;
}
static void hdr_ipr_in_jp2h(Jp2Family *f, int box) {
    (void) box;
    jp2h_of(f)->children.arr[0].payload.u.ihdr.ipr = 1;
    set_ipr_box(append_child(jp2h_of(f)));
}
/* A JPX file named .jp2 (T.801 M.2.1: 'jp2 ' in its list for JP2
 * readers): the brand 'jpx ', MinV 1, rreq third, APPROX 1. */
static void hdr_jpx_named_jp2(Jp2Family *f, int box) {
    Ftyp *ftyp = ftyp_of(f);
    (void) box;
    ftyp->header.brand = HV_BRAND_JPX;
    ftyp->header.minor = 1;
    ftyp->compat.nCount = 2;
    ftyp->compat.arr[0] = HV_BRAND_JPX;
    ftyp->compat.arr[1] = HV_BRAND_JP2;
    add_rreq(f, 0);
    move_box(f, f->boxes.nCount - 1, 2);
    jp2h_of(f)->children.arr[1].payload.u.colr.header.approx = 1;
}

/* JPX mutants. */
static Superbox *add_jplh(Jp2Family *f) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->payload.kind = TopPayload_jplh_PRESENT;
    b->payload.u.jplh.children.nCount = 0;
    return &b->payload.u.jplh;
}
static void hdr_jpx_jp2h_in_jplh(Jp2Family *f, int box) {
    InnerBox *c;
    (void) box;
    c = append_child(add_jplh(f));
    c->payload.kind = InnerPayload_jp2h_PRESENT;
    c->payload.u.jp2h.data.nCount = 0;
}
static void hdr_jpx_rreq_in_asoc(Jp2Family *f, int box) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    InnerBox *c;
    (void) box;
    b->payload.kind = TopPayload_asoc_PRESENT;
    b->payload.u.asoc.children.nCount = 2;
    c = &b->payload.u.asoc.children.arr[0];
    c->payload.kind = InnerPayload_lbl_PRESENT;
    OCTETS(c->payload.u.lbl.data, "rreq", 4);
    c = &b->payload.u.asoc.children.arr[1];
    c->payload.kind = InnerPayload_other_PRESENT;
    OCTETS(c->payload.u.other.data, "\x01\x80\x80\x00\x00\x00\x00", 7);
    queue_type("rreq");
}
static void hdr_jpx_colr_in_jpch(Jp2Family *f, int box) {
    (void) box;
    set_colr(f, append_child(jpch_of(f)), 1, 17);
}
static void hdr_jpx_opct(Jp2Family *f, int box) { (void) box; other_child(add_jplh(f), "opct", "\x00", 1); }
static void hdr_jpx_opct_in_jpch(Jp2Family *f, int box) {
    (void) box;
    other_child(jpch_of(f), "opct", "\x00", 1);
}
static void hdr_jpx_two_opct(Jp2Family *f, int box) {
    Superbox *l = add_jplh(f);
    (void) box;
    other_child(l, "opct", "\x00", 1);
    other_child(l, "opct", "\x00", 1);
}
static void hdr_jpx_opct_cdef(Jp2Family *f, int box) {
    Superbox *l = add_jplh(f);
    (void) box;
    other_child(l, "opct", "\x00", 1);
    set_cdef(append_child(l), 1, 0, 0, 1);
}
static void hdr_jpx_creg_in_jpch(Jp2Family *f, int box) { (void) box; set_creg(append_child(jpch_of(f))); }
static void hdr_jpx_two_creg(Jp2Family *f, int box) {
    Superbox *l = add_jplh(f);
    (void) box;
    set_creg(append_child(l));
    set_creg(append_child(l));
}
static void hdr_jpx_two_pxfm(Jp2Family *f, int box) {
    (void) box;
    other_child(jp2h_of(f), "pxfm", "\x00\x00", 2);
    other_child(jp2h_of(f), "pxfm", "\x00\x00", 2);
}
/* A cref for this file's jp2h's colr (Rtyp 'colr', a flst of one
 * fragment in this file). */
static const unsigned char cref_payload[] = {
    'c', 'o', 'l', 'r', 0x00, 0x00, 0x00, 0x18, 'f', 'l', 's', 't', 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };
static void hdr_jpx_cref(Jp2Family *f, int box) {
    (void) box;
    other_child(jpch_of(f), "cref", cref_payload, sizeof cref_payload);
}
static void hdr_jpx_top_cref(Jp2Family *f, int box) {
    (void) box;
    other_top(f, "cref", cref_payload, sizeof cref_payload);
}
/* A Composition box: copt (HEIGHT, WIDTH, LOOP) and an inst, as `first`
 * says. */
static void add_comp(Jp2Family *f, int copt_first) {
    unsigned char payload[64];
    size_t n = 0;
    static const unsigned char copt[] = { 0, 0, 0, 4, 0, 0, 0, 4, 0 };
    static const unsigned char inst[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (copt_first) n += put_box(payload + n, "copt", copt, sizeof copt);
    n += put_box(payload + n, "inst", inst, sizeof inst);
    other_top(f, "comp", payload, n);
}
static void hdr_jpx_comp(Jp2Family *f, int box) { (void) box; add_comp(f, 1); }
static void hdr_jpx_two_comp(Jp2Family *f, int box) { (void) box; add_comp(f, 1); add_comp(f, 1); }
static void hdr_jpx_comp_inst_first(Jp2Family *f, int box) { (void) box; add_comp(f, 0); }
static void hdr_jpx_copt_in_jpch(Jp2Family *f, int box) {
    (void) box;
    other_child(jpch_of(f), "copt", "\x00\x00\x00\x04\x00\x00\x00\x04\x00", 9);
}
/* A Desired Reproductions box holding `gtsos` gtso boxes. */
static void add_drep(Jp2Family *f, int gtsos) {
    unsigned char payload[64];
    size_t n = 0;
    int i;
    for (i = 0; i < gtsos; ++i) n += put_box(payload + n, "gtso", "\x00\x00\x00\x00", 4);
    other_top(f, "drep", payload, n);
}
static void hdr_jpx_drep(Jp2Family *f, int box) { (void) box; add_drep(f, 1); }
static void hdr_jpx_two_drep(Jp2Family *f, int box) { (void) box; add_drep(f, 1); add_drep(f, 1); }
static void hdr_jpx_two_gtso(Jp2Family *f, int box) { (void) box; add_drep(f, 2); }
static void hdr_jpx_top_gtso(Jp2Family *f, int box) {
    (void) box;
    other_top(f, "gtso", "\x00\x00\x00\x00", 4);
}
/* An asoc of a label and `inner`, raw boxes the model keeps opaque. */
static void add_asoc(Jp2Family *f, const char *label, const void *inner, size_t n) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    InnerBox *c;
    b->payload.kind = TopPayload_asoc_PRESENT;
    b->payload.u.asoc.children.nCount = 2;
    c = &b->payload.u.asoc.children.arr[0];
    c->payload.kind = InnerPayload_lbl_PRESENT;
    OCTETS(c->payload.u.lbl.data, label, strlen(label));
    c = &b->payload.u.asoc.children.arr[1];
    c->payload.kind = InnerPayload_asoc_PRESENT;
    OCTETS(c->payload.u.asoc.data, inner, n);
}
static void hdr_jpx_asoc_one_child(Jp2Family *f, int box) {
    (void) box;
    add_asoc(f, "outer", "\x00\x00\x00\x0D""lbl inner", 13);
}
static void hdr_jpx_lbl_characters(Jp2Family *f, int box) {
    (void) box;
    add_asoc(f, "a/b", "\x00\x00\x00\x09""lbl a""\x00\x00\x00\x09""lbl b", 18);
}
static void hdr_jpx_asoc_framing(Jp2Family *f, int box) {
    (void) box;
    add_asoc(f, "outer", "\x00\x00\x00\x09""lbl a""\x00\x00\x00\x40""lbl b", 18);
}
static void hdr_jpx_no_colr(Jp2Family *f, int box) {
    (void) box;
    remove_box(f, box_index(f, TopPayload_jp2h_PRESENT, 0));
}
/* A Multiple Codestream box (T.801 M.11.23) at the end: a j2ci of Ncs
 * `ncs` and Ltbl `ltbl` (unless `ncs` is negative), `mdat_first` an empty
 * mdat, then `streams` codestreams; a jpch for each, after the first. */
static void add_j2cx(Jp2Family *f, long ncs, uint32_t ltbl, int streams, int mdat_first) {
    Jp2Family *cs = xcalloc(sizeof *cs);
    Bytes stream;
    unsigned char payload[1024], info[8];
    size_t n = 0;
    int i;
    add_jp2c(cs, BASE_W, BASE_H, 0, 0, 1);
    stream = encode_family(cs, 0);                  /* one jp2c box */
    if (ncs >= 0) {
        put32(info, (uint32_t) ncs);
        put32(info + 4, ltbl);
        n += put_box(payload + n, "j2ci", info, sizeof info);
    }
    if (mdat_first) n += put_box(payload + n, "mdat", "", 0);
    for (i = 0; i < streams; ++i) {
        if (n + stream.len > sizeof payload) die("add_j2cx: codestreams too long");
        memcpy(payload + n, stream.data, stream.len);
        n += stream.len;
        add_jpch(f);
        move_box(f, f->boxes.nCount - 1, box_index(f, TopPayload_jp2c_PRESENT, 0));
    }
    other_top(f, "j2cx", payload, n);
    free(stream.data);
    free(cs);
}
/* A j2cx with a j2ci (Ncs 1, Ltbl 0) and a codestream, and a third jpch. */
static void hdr_jpx_j2cx(Jp2Family *f, int box) { (void) box; add_j2cx(f, 1, 0, 1, 0); }
static void hdr_jpx_j2cx_no_info(Jp2Family *f, int box) { (void) box; add_j2cx(f, -1, 0, 1, 0); }
static void hdr_jpx_j2cx_ncs(Jp2Family *f, int box) { (void) box; add_j2cx(f, 2, 0, 1, 0); }
static void hdr_jpx_j2cx_mdat(Jp2Family *f, int box) { (void) box; add_j2cx(f, 1, 0, 1, 1); }
/* Two codestreams, Ltbl L 1, R 0: the first is longer than 1 byte. */
static void hdr_jpx_j2cx_ltbl(Jp2Family *f, int box) { (void) box; add_j2cx(f, 2, 1, 2, 0); }
/* Two codestreams, Ltbl their length, R 0: valid. */
static void hdr_jpx_j2cx_two(Jp2Family *f, int box) {
    Jp2Family *cs = xcalloc(sizeof *cs);
    Bytes stream;
    (void) box;
    add_jp2c(cs, BASE_W, BASE_H, 0, 0, 1);
    stream = encode_family(cs, 0);
    add_j2cx(f, 2, (uint32_t) stream.len, 2, 0);
    free(stream.data);
    free(cs);
}
/* Three top-level jplh, for the two top-level codestreams, and a j2cx. */
static void hdr_jpx_j2cx_layers(Jp2Family *f, int box) {
    (void) box;
    add_jplh(f);
    add_jplh(f);
    add_jplh(f);
    add_j2cx(f, 1, 0, 1, 0);
}
/* hdr_jpx_j2cx's j2cx before the second top-level jp2c. */
static void hdr_jpx_j2cx_early(Jp2Family *f, int box) {
    hdr_jpx_j2cx(f, box);
    move_box(f, f->boxes.nCount - 1, box_index(f, TopPayload_jp2c_PRESENT, 1));
}
/* A Compositing Layer Extensions box (T.801 M.11.21) at the end: a jlxi
 * of M, C, L, T and F (and LIFE-START 0 when T is not 0), unless `m` is
 * negative, then the n bytes of boxes. */
static void add_jclx_with(Jp2Family *f, long m, uint32_t c, uint32_t l, uint32_t t, uint32_t fr,
                          const void *boxes, size_t n) {
    unsigned char payload[256], info[24];
    size_t k = 0;
    if (m >= 0) {
        put32(info, (uint32_t) m);
        put32(info + 4, c);
        put32(info + 8, l);
        put32(info + 12, t);
        put32(info + 16, fr);
        put32(info + 20, 0);
        k += put_box(payload + k, "jlxi", info, t != 0 ? 24 : 20);
    }
    if (k + n > sizeof payload) die("add_jclx_with: too long");
    memcpy(payload + k, boxes, n);
    other_top(f, "jclx", payload, k + n);
}
/* One compositing group of an empty jplh, no inst: M 1, C 0, L 1, T 0. */
static const unsigned char one_jplh[] = { 0, 0, 0, 8, 'j', 'p', 'l', 'h' };
static void add_jclx(Jp2Family *f) { add_jclx_with(f, 1, 0, 1, 0, 0, one_jplh, sizeof one_jplh); }
static void hdr_jpx_jclx(Jp2Family *f, int box) {
    (void) box;
    add_jplh(f);
    add_comp(f, 1);
    add_jclx(f);
}
static void hdr_jpx_jclx_alone(Jp2Family *f, int box) { (void) box; add_jplh(f); add_jclx(f); }
static void hdr_jpx_jclx_boxes(Jp2Family *f, long m, uint32_t c, uint32_t l, uint32_t t,
                               uint32_t fr, const void *boxes, size_t n) {
    add_jplh(f);
    add_comp(f, 1);
    add_jclx_with(f, m, c, l, t, fr, boxes, n);
}
static void hdr_jpx_jclx_no_info(Jp2Family *f, int box) {
    (void) box;
    hdr_jpx_jclx_boxes(f, -1, 0, 0, 0, 0, one_jplh, sizeof one_jplh);
}
static void hdr_jpx_jclx_counts(Jp2Family *f, int box) {
    (void) box;
    hdr_jpx_jclx_boxes(f, 1, 0, 2, 0, 0, one_jplh, sizeof one_jplh);
}
static void hdr_jpx_jclx_repetition(Jp2Family *f, int box) {
    (void) box;
    hdr_jpx_jclx_boxes(f, 0, 0, 1, 0, 0, one_jplh, sizeof one_jplh);
}
/* An inst (T.801 M.11.10.2: Ityp, REPT, TICK) before the group's jplh. */
static void hdr_jpx_jclx_inst_first(Jp2Family *f, int box) {
    static const unsigned char boxes[] = { 0, 0, 0, 16, 'i', 'n', 's', 't', 0, 0, 0, 0, 0, 0, 0, 0,
                                           0, 0, 0, 8, 'j', 'p', 'l', 'h' };
    (void) box;
    hdr_jpx_jclx_boxes(f, 1, 0, 1, 1, 1, boxes, sizeof boxes);
}
/* M.11.10.2: Ityp 0 (no instructions), REPT 0, TICK 0. Reuse the
 * same payload in each placement so that only its parent changes. */
static const unsigned char inst_payload[8] = {0};
static void hdr_inst_top(Jp2Family *f, int box) {
    (void) box;
    other_top(f, "inst", inst_payload, sizeof inst_payload);
}
static void hdr_inst_jpch(Jp2Family *f, int box) {
    (void) box;
    other_child(jpch_of(f), "inst", inst_payload, sizeof inst_payload);
}
static void hdr_inst_asoc(Jp2Family *f, int box) {
    unsigned char payload[25];
    size_t n = put_box(payload, "lbl ", "a", 1);
    (void) box;
    n += put_box(payload + n, "inst", inst_payload, sizeof inst_payload);
    other_top(f, "asoc", payload, n);
}
static void hdr_inst_comp(Jp2Family *f, int box) {
    unsigned char payload[33];
    static const unsigned char copt[] = {0, 0, 0, 4, 0, 0, 0, 4, 0};
    size_t n = put_box(payload, "copt", copt, sizeof copt);
    (void) box;
    n += put_box(payload + n, "inst", inst_payload, sizeof inst_payload);
    other_top(f, "comp", payload, n);
}
static void hdr_inst_jclx(Jp2Family *f, int box) {
    unsigned char boxes[24];
    size_t n = put_box(boxes, "jplh", "", 0);
    (void) box;
    n += put_box(boxes + n, "inst", inst_payload, sizeof inst_payload);
    add_jplh(f);
    hdr_inst_comp(f, box);
    add_jclx_with(f, 1, 0, 1, 1, 1, boxes, n);
}
/* A jclx whose thread has F 0, then another jclx: F 0 only in the last. */
static void hdr_jpx_jclx_frames(Jp2Family *f, int box) {
    static const unsigned char boxes[] = { 0, 0, 0, 8, 'j', 'p', 'l', 'h',
                                           0, 0, 0, 16, 'i', 'n', 's', 't', 0, 0, 0, 0, 0, 0, 0, 0 };
    (void) box;
    hdr_jpx_jclx_boxes(f, 1, 0, 1, 1, 0, boxes, sizeof boxes);
    add_jclx(f);
}
/* An asoc whose first box is a grp holding a lbl, then an xml. */
static void hdr_jpx_asoc_grp_first(Jp2Family *f, int box) {
    static const unsigned char boxes[] = { 0, 0, 0, 17, 'g', 'r', 'p', ' ',
                                           0, 0, 0, 9, 'l', 'b', 'l', ' ', 'a',
                                           0, 0, 0, 12, 'x', 'm', 'l', ' ', '<', 'a', '/', '>' };
    (void) box;
    other_top(f, "asoc", boxes, sizeof boxes);
}
static void hdr_jpx_jclx_early(Jp2Family *f, int box) {
    (void) box;
    add_jclx(f);
    move_box(f, f->boxes.nCount - 1, box_index(f, TopPayload_jpch_PRESENT, 0));
}
static void hdr_jpx_deprecated_feature(Jp2Family *f, int box) {
    (void) box;
    f->boxes.arr[box_index(f, TopPayload_rreq_PRESENT, 0)].payload.u.rreq.standard.arr[0].sf = 3;
}
static void hdr_jpx_ihdr_c13(Jp2Family *f, int box) { (void) box; jpch_of(f)->children.arr[0].payload.u.ihdr.c = 13; }
static void hdr_jpx_ihdr_c0(Jp2Family *f, int box) { (void) box; jpch_of(f)->children.arr[0].payload.u.ihdr.c = 0; }
static void hdr_jpx_bpcc_in_jp2h(Jp2Family *f, int box) {
    (void) box;
    jpch_of(f)->children.arr[0].payload.u.ihdr.bpc = 255;
    set_bpcc(append_child(jp2h_of(f)), 1, 7);
}
static void hdr_jpx_part2_depth(Jp2Family *f, int box) {
    cs_of(f, box)->siz.body.fixed.rsiz = 0x8100;
    jpch_of(f)->children.arr[0].payload.u.ihdr.bpc = 8;
}
static void hdr_jpx_ipr_in_jplh(Jp2Family *f, int box) {
    (void) box;
    jpch_of(f)->children.arr[0].payload.u.ihdr.ipr = 1;
    set_ipr_box(append_child(add_jplh(f)));
}
static void jpx_list_jp2(Jp2Family *f) {
    Ftyp *ftyp = ftyp_of(f);
    ftyp->compat.arr[ftyp->compat.nCount++] = HV_BRAND_JP2;
}
static void hdr_jpx_jp2_compatible(Jp2Family *f, int box) { (void) box; jpx_list_jp2(f); }
static void hdr_jpx_jp2_no_jp2h(Jp2Family *f, int box) { hdr_jpx_no_jp2h(f, box); jpx_list_jp2(f); }
static void hdr_jpx_jp2_cielab(Jp2Family *f, int box) {
    (void) box;
    jpx_list_jp2(f);
    jp2h_of(f)->children.arr[1].payload.u.colr.header.enumcs = 14;
}
static void jpx_list_jpxb(Jp2Family *f) {
    Ftyp *ftyp = ftyp_of(f);
    ftyp->compat.arr[ftyp->compat.nCount++] = HV_BRAND_JPXB;
}
static void hdr_jpxb_late_jp2h(Jp2Family *f, int box) { hdr_jpx_late_jp2h(f, box); jpx_list_jpxb(f); }
static void hdr_jpxb_override(Jp2Family *f, int box) { (void) box; jpx_list_jpxb(f); }
static void hdr_jpxb_colour(Jp2Family *f, int box) {
    (void) box;
    jpx_list_jpxb(f);
    jpch_of(f)->children.nCount = 0;
    jp2h_of(f)->children.arr[1].payload.u.colr.header.enumcs = 12;
}
static void hdr_jpx_top_lbl(Jp2Family *f, int box) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    (void) box;
    b->payload.kind = TopPayload_lbl_PRESENT;
    OCTETS(b->payload.u.lbl.data, "label", 5);
}

static const RuleMutant header_mutants[] = {
    { "jp2h.palette", hdr_palette, "pclr (2 entries) and cmap through it: valid", CF_JP2, 0,
      X_VALID, NULL, NULL },
    { "jp2h.resolution", hdr_resolution, "res with resc and resd: valid", CF_JP2, 0, X_VALID, NULL,
      NULL },
    { "jp2h.other-box", hdr_other_child,
      "xml box in jp2h after colr: valid and skipped (T.800 I.5.3)", CF_JP2, 0, X_VALID, NULL,
      NULL },
    { "jp2h.second-colr", hdr_second_colr,
      "second enumerated colr (greyscale): valid, readers use the first", CF_JP2, 0, X_VALID, NULL,
      NULL },
    { "colr.prec-approx", hdr_colr_prec_approx,
      "PREC -1 and APPROX 1 (T.800 Table I.11: 0; readers ignore them)", CF_JP2, 0, X_LENIENT,
      "colr.prec-approx", NULL },
    { "cdef.pairs", hdr_cdef_unspecified,
      "grey and two unspecified channels from a palette: the pair (65535, 65535) may repeat, valid",
      CF_JP2, 0, X_VALID, NULL, NULL },
    { "res.resc-resd", hdr_res_other, "resd and an unknown box in res: valid", CF_JP2, 0, X_VALID,
      NULL, NULL },
    { "jp2.one-jp2h", hdr_no_jp2h, "no jp2h (T.800 I.5.3)", CF_JP2, 0, X_LENIENT, "jp2.one-jp2h",
      NULL },
    { "jp2.one-jp2h", hdr_two_jp2h, "two jp2h boxes", CF_JP2, 0, X_LENIENT, "jp2.one-jp2h", NULL },
    { "jp2h.position", hdr_jp2h_after_jp2c, "jp2h after jp2c (T.800 I.2)", CF_JP2, 0, X_LENIENT,
      "jp2h.position", NULL },
    { "jp2.one-codestream", hdr_no_jp2c, "no jp2c (T.800 I.2)", CF_JP2, 0, X_STD,
      "jp2.one-codestream", NULL },
    { "jp2h.ihdr-first", hdr_colr_first, "colr before ihdr", CF_JP2, 0, X_LENIENT,
      "jp2h.ihdr-first", NULL },
    { "jp2h.colr", hdr_no_colr, "no colr", CF_JP2, 0, X_LENIENT, "jp2h.colr", NULL },
    { "jp2h.colr-contiguous", hdr_colr_split, "colr, xml, colr", CF_JP2, 0, X_LENIENT,
      "jp2h.colr-contiguous", NULL },
    { "ihdr.bpcc", hdr_bpcc_not_varying, "bpcc with BPC 7 (I.5.3.2: only when BPC is 255)", CF_JP2,
      0, X_LENIENT, "ihdr.bpcc", NULL },
    { "ihdr.bpcc", hdr_bpc_255_no_bpcc, "BPC 255 without bpcc", CF_JP2, 0, X_LENIENT, "ihdr.bpcc",
      NULL },
    { "header.one-bpcc", hdr_two_bpcc, "two bpcc", CF_JP2, 0, X_LENIENT, "header.one-bpcc", NULL },
    { "bpcc.count", hdr_bpcc_count,
      "BPC 255, bpcc of 2 entries for NC 1 (and BPC 255 for equal depths)", CF_JP2, 0, X_LENIENT,
      "bpcc.count", NULL },
    { "bpcc.depth", hdr_bpcc_depth,
      "BPC 255, bpcc entry 8 for Ssiz 7 (and BPC 255 for equal depths)", CF_JP2, 0, X_LENIENT,
      "bpcc.depth", NULL },
    { "ihdr.height", hdr_ihdr_height, "HEIGHT = Ysiz - YOsiz + 1", CF_JP2, 0, X_LENIENT,
      "ihdr.height", NULL },
    { "ihdr.width", hdr_ihdr_width, "WIDTH = Xsiz - XOsiz + 1", CF_JP2, 0, X_LENIENT, "ihdr.width",
      NULL },
    { "ihdr.nc", hdr_ihdr_nc, "NC 2 for Csiz 1", CF_JP2, 0, X_LENIENT, "ihdr.nc", NULL },
    { "ihdr.bpc", hdr_ihdr_bpc, "BPC 8 for Ssiz 7", CF_JP2, 0, X_LENIENT, "ihdr.bpc", NULL },
    { "ihdr.bpc", hdr_ihdr_signed, "BPC signed for unsigned Ssiz", CF_JP2, 0, X_LENIENT, "ihdr.bpc",
      NULL },
    { "ihdr.c", hdr_ihdr_c, "C 0 (uncompressed; T.800 I.5.3.1: 7)", CF_JP2, 0, X_LENIENT,
      "ihdr.c", NULL },
    { "ihdr.unkc", hdr_ihdr_unkc, "UnkC 2 (reserved)", CF_JP2, 0, X_LENIENT, "decode", NULL },
    { "ihdr.nc", hdr_ihdr_nc_zero, "NC 0", CF_JP2, 0, X_LENIENT, "decode", NULL },
    { "ihdr.bpc", hdr_ihdr_bpc_38, "BPC 38 (reserved)", CF_JP2, 0, X_LENIENT, "decode", NULL },
    { "colr.method", hdr_colr_method, "METH 3 (JP2: 1 or 2)", CF_JP2, 0, X_LENIENT, "colr.method",
      NULL },
    { "colr.enumcs", hdr_colr_enumcs, "first colr EnumCS 12 (CMYK; JP2: 16, 17 or 18)", CF_JP2, 0,
      X_LENIENT, "colr.enumcs", NULL },
    { "colr.enumcs-length", hdr_colr_enumcs_length, "METH 1 with a byte after EnumCS", CF_JP2, 0,
      X_LENIENT, "colr.enumcs-length", NULL },
    { "header.pclr-cmap", hdr_pclr_no_cmap, "pclr without cmap", CF_JP2, 0, X_LENIENT,
      "header.pclr-cmap", NULL },
    { "header.pclr-cmap", hdr_cmap_no_pclr, "cmap without pclr", CF_JP2, 0, X_LENIENT,
      "header.pclr-cmap", NULL },
    { "header.one-pclr", hdr_two_pclr, "two pclr", CF_JP2, 0, X_LENIENT, "header.one-pclr", NULL },
    { "header.one-cmap", hdr_two_cmap, "two cmap", CF_JP2, 0, X_LENIENT, "header.one-cmap", NULL },
    { "pclr.entries-length", hdr_pclr_entries, "NE 2 x 1 byte, 1 byte of entries", CF_JP2, 0,
      X_LENIENT, "pclr.entries-length", NULL },
    { "pclr.ne", hdr_pclr_ne_zero, "NE 0", CF_JP2, 0, X_LENIENT, "decode", NULL },
    { "cmap.pcol-zero", hdr_cmap_pcol_zero, "MTYP 0 with PCOL 1", CF_JP2, 0, X_LENIENT,
      "cmap.pcol-zero", NULL },
    { "cmap.component", hdr_cmap_component, "CMP 1 for NC 1", CF_JP2, 0, X_LENIENT,
      "cmap.component", NULL },
    { "cmap.palette-column", hdr_cmap_palette_column, "PCOL 1 for NPC 1", CF_JP2, 0, X_LENIENT,
      "cmap.palette-column", NULL },
    { "cmap.mtyp", hdr_cmap_mtyp, "MTYP 2 (reserved)", CF_JP2, 0, X_LENIENT, "decode", NULL },
    { "cdef.pairs", hdr_cdef_pairs,
      "channel 0 described twice as Typ 0, Asoc 1 (I.5.3.6: multiple descriptions for a single "
      "channel): valid", CF_JP2, 0, X_VALID, NULL, NULL },
    { "cdef.channel", hdr_cdef_channel, "Cn 1 for one channel", CF_JP2, 0, X_LENIENT,
      "cdef.channel", NULL },
    { "cdef.typ", hdr_cdef_typ, "Typ 3 (reserved)", CF_JP2, 0, X_LENIENT, "decode", NULL },
    { "header.one-cdef", hdr_two_cdef, "two cdef", CF_JP2, 0, X_LENIENT, "header.one-cdef", NULL },
    { "header.one-res", hdr_two_res, "two res", CF_JP2, 0, X_LENIENT, "header.one-res", NULL },
    { "res.resc-resd", hdr_res_two_resc,
      "two resc in res (I.5.3.7: resc, resd or both; no limit): valid", CF_JP2, 0, X_VALID, NULL,
      NULL },
    { "res.resc-resd", hdr_res_empty, "res with neither resc nor resd", CF_JP2, 0, X_LENIENT,
      "res.resc-resd", NULL },
    { "res.vd", hdr_res_zero, "resc VRcD 0", CF_JP2, 0, X_LENIENT, "decode", NULL },
    { "cdef.opacity", hdr_cdef_opacity, "grey and opacity channels from a palette: valid", CF_JP2,
      0, X_VALID, NULL, NULL },
    { "jpx.header-defaults", hdr_jpx_defaults,
      "jp2h ihdr and colr as the default for both codestreams, empty jpch: valid", CF_JPX, 0,
      X_VALID, NULL, NULL },
    { "jpch.palette", hdr_jpx_palette, "pclr and cmap in a jpch: valid", CF_JPX, 0, X_VALID, NULL,
      NULL },
    { "jpx.no-jp2h", hdr_jpx_no_jp2h,
      "no jp2h, an ihdr in each jpch, the colours in a jplh's cgrp (T.801 M.11.5, M.11.7.1): valid",
      CF_JPX, 0, X_VALID, NULL, NULL },
    { "jplh.res", hdr_jpx_jplh, "jplh with res: valid", CF_JPX, 0, X_VALID, NULL, NULL },
    { "jpch.ihdr", hdr_jpx_no_ihdr, "no jp2h, first jpch empty (T.801 M.11.6)", CF_JPX, 0,
      X_LENIENT, "jpch.ihdr", NULL },
    { "jp2h.position", hdr_jpx_late_jp2h,
      "jp2h after the codestreams, no 'jpxb' (T.801 M.11.5: anywhere at the top level): valid",
      CF_JPX, 0, X_VALID, NULL, NULL },
    { "header.one-ihdr", hdr_jpx_two_ihdr, "two ihdr in a jpch", CF_JPX, 0, X_LENIENT,
      "header.one-ihdr", NULL },
    { "ihdr.width", hdr_jpx_ihdr_width, "jpch ihdr WIDTH = Xsiz - XOsiz + 1", CF_JPX, 0, X_LENIENT,
      "ihdr.width", NULL },
    { "header.pclr-cmap", hdr_jpx_pclr_no_cmap, "pclr in a jpch without cmap", CF_JPX, 0, X_LENIENT,
      "header.pclr-cmap", NULL },
    { "colr.one-method", hdr_jpx_two_enumerated, "two enumerated colr in jp2h (T.801 M.11.7.1)",
      CF_JPX, 0, X_LENIENT, "colr.one-method", NULL },
    { "cdef.pairs", hdr_jpx_jplh_cdef_pairs,
      "jplh cdef describing channel 0 twice as Typ 0, Asoc 1: valid", CF_JPX, 0, X_VALID, NULL,
      NULL },
    { "rreq.mask-length", hdr_rreq_ml2, "rreq masks of 2 bytes (ML 2): valid", CF_JPX, 0, X_VALID,
      NULL, NULL },
    { "rreq.ml", hdr_rreq_ml3, "rreq masks of 3 bytes (ML 3; T.801 Table M.15: 1, 2, 4 or 8)",
      CF_JPX, 0, X_LENIENT, "decode", NULL },
    { "ihdr.extent", hdr_ihdr_extent, "a byte after the ihdr fields: a 23-byte box (I.5.3.1: 22)",
      CF_JP2, 0, X_LENIENT, "ihdr.extent", NULL },
    { "cdef.extent", hdr_cdef_extent, "a byte after the cdef descriptions", CF_JP2, 0, X_LENIENT,
      "cdef.extent", NULL },
    { "res.extent", hdr_res_extent, "a byte after the resc fields", CF_JP2, 0, X_LENIENT,
      "res.extent", NULL },
    { "box.nested-jp2c", hdr_jp2c_in_jp2h, "jp2c inside jp2h (T.800 I.2: at the top level)", CF_JP2,
      0, X_LENIENT, "box.nested-jp2c", NULL },
    { "box.flst-placement", hdr_flst_in_jp2h, "flst inside jp2h (T.801 M.11.3: in ftbl)", CF_JPX, 0,
      X_LENIENT, "box.flst-placement", NULL },
    { "colr.cielab-parameters", hdr_jpx_cielab_ep, "EnumCS 14 (CIELab) with its 28-byte EP: valid",
      CF_JPX, 0, X_VALID, NULL, NULL },
    { "colr.ciejab-parameters", hdr_jpx_ciejab_ep, "EnumCS 19 (CIEJab) with its 24-byte EP: valid",
      CF_JPX, 0, X_VALID, NULL, NULL },
    { "colr.enumcs-length", hdr_jpx_cielab_short_ep,
      "EnumCS 14 with 4 bytes after it (T.801 M.11.7.4.1: 28 or none)", CF_JPX, 0, X_LENIENT,
      "colr.enumcs-length", NULL },
    { "colr.approx", hdr_jpx_approx_zero, "APPROX 0 (T.801 M.11.7.2: illegal in JPX)", CF_JPX, 0,
      X_LENIENT, "colr.approx", NULL },
    { "colr.approx", hdr_jpx_approx_five, "APPROX 5 (T.801 Table M.23: 1 to 4)", CF_JPX, 0,
      X_LENIENT, "colr.approx", NULL },
    { "file.ftyp-minor", hdr_jpx_minor_zero, "MinV 0 (T.801 M.8: 1)", CF_JPX, 0, X_LENIENT,
      "file.ftyp-minor", NULL },
    { "ihdr.ipr", hdr_ipr_flag_no_box, "IPR 1 without an IPR box (T.800 I.5.3.1)", CF_JP2, 0,
      X_LENIENT, "ihdr.ipr", NULL },
    { "ihdr.ipr", hdr_ipr_box_no_flag, "a top-level IPR box with IPR 0 (T.800 I.5.3.1)", CF_JP2, 0,
      X_LENIENT, "ihdr.ipr", NULL },
    { "jp2.ipr", hdr_ipr_flag_and_box, "IPR 1 and a top-level IPR box: valid", CF_JP2, 0, X_VALID,
      NULL, NULL },
    { "jpch.ipr", hdr_jpx_jpch_ipr_no_flag, "IPR box in a jpch whose ihdr has IPR 0 (M.11.6)",
      CF_JPX, 0, X_LENIENT, "jpch.ipr", NULL },
    { "jpch.ipr-box", hdr_jpx_jpch_ipr_flag, "IPR box in a jpch whose ihdr has IPR 1: valid",
      CF_JPX, 0, X_VALID, NULL, NULL },
    { "jpx.creg", hdr_jpx_creg_one, "creg in one of two jplh (T.801 M.11.7: in every one)", CF_JPX,
      0, X_LENIENT, "jpx.creg", NULL },
    { "jplh.creg", hdr_jpx_creg_both, "creg in both jplh: valid", CF_JPX, 0, X_VALID, NULL, NULL },
    { "jpx.one-jp2h", hdr_jpx_two_jp2h, "two jp2h (T.801 M.11.5: at most one)", CF_JPX, 0,
      X_LENIENT, "jpx.one-jp2h", NULL },
    { "jp2h.ihdr-first", hdr_empty_jp2h, "an empty jp2h (T.800 I.5.3: ihdr first)", CF_JP2, 0,
      X_LENIENT, "jp2h.ihdr-first", NULL },
    { "ihdr.bpcc", hdr_jpx_bpc_255_no_bpcc, "jpch ihdr BPC 255, no bpcc in jpch or jp2h",
      CF_JPX, 0, X_LENIENT, "ihdr.bpcc", NULL },
    { "jplh.cgrp", hdr_jpx_cgrp, "jplh with a cgrp of one colr and an unknown box: valid", CF_JPX,
      0, X_VALID, NULL, NULL },
    { "colr.approx", hdr_jpx_cgrp_approx_zero, "cgrp colr APPROX 0 (T.801 M.11.7.2)", CF_JPX, 0,
      X_LENIENT, "colr.approx", NULL },
    { "colr.one-method", hdr_jpx_cgrp_two_enumerated,
      "two enumerated colr in a cgrp (T.801 M.11.7.1)", CF_JPX, 0, X_LENIENT, "colr.one-method",
      NULL },
    { "cgrp.empty", hdr_jpx_cgrp_no_colr, "cgrp without colr (T.801 M.11.7.1)", CF_JPX, 0,
      X_LENIENT, "cgrp.empty", NULL },
    { "cgrp.placement", hdr_jpx_cgrp_in_jpch, "cgrp in a jpch (T.801 M.11.7.1: only in jplh)",
      CF_JPX, 0, X_LENIENT, "cgrp.placement", NULL },
    { "cgrp.placement", hdr_jpx_cgrp_top, "cgrp at the top level (T.801 M.11.7.1: only in jplh)",
      CF_JPX, 0, X_LENIENT, "cgrp.placement", NULL },
    { "jp2h.cgrp", hdr_cgrp_in_jp2h, "cgrp in a JP2 jp2h (T.800 I.8: an unknown box): valid",
      CF_JP2, 0, X_VALID, NULL, NULL },
    { "box.top-level", hdr_top_colr, "colr at the top level (T.800 I.2.2, Figure I.1: in jp2h)",
      CF_JP2, 0, X_LENIENT, "box.top-level", NULL },
    { "colr.placement", hdr_top_colr,
      "colr at the top level (T.801 M.11.7.2: in jp2h or a cgrp)", CF_JPX, 0, X_LENIENT,
      "colr.placement", NULL },
    { "jpx.ipr", hdr_jpx_jpch_ipr_flag_no_box,
      "jpch ihdr IPR 1 without an IPR box (T.801 M.11.5.1)", CF_JPX, 0, X_LENIENT, "jpx.ipr",
      NULL },
    { "jpx.ipr-top", hdr_jpx_jpch_ipr_flag_top_box,
      "jpch ihdr IPR 1 and a top-level IPR box: valid", CF_JPX, 0, X_VALID, NULL, NULL },
    /* The box tree (hv_rule_box_tree). */
    { "file.one-signature", hdr_second_signature, "a second jP box (T.800 I.5.1: one and only one)",
      CF_JP2, 0, X_LENIENT, "file.one-signature", NULL },
    { "file.one-signature", hdr_second_signature, "a second jP box (T.800 I.5.1: one and only one)",
      CF_JPX, 0, X_LENIENT, "file.one-signature", NULL },
    { "file.one-ftyp", hdr_second_ftyp, "a second ftyp box (T.800 I.5.2: one and only one)",
      CF_JP2, 0, X_LENIENT, "file.one-ftyp", NULL },
    { "file.one-ftyp", hdr_second_ftyp, "a second ftyp box (T.800 I.5.2: one and only one)",
      CF_JPX, 0, X_LENIENT, "file.one-ftyp", NULL },
    { "file.ftyp-minor", hdr_jp2_minor_one, "MinV 1 (T.800 I.5.2: 0)", CF_JP2, 0, X_LENIENT,
      "file.ftyp-minor", NULL },
    { "jp2.uinf", hdr_uinf, "uinf with a ulst of one UUID and a url (T.800 I.7.3): valid", CF_JP2,
      0, X_VALID, NULL, NULL },
    { "box.nested-top-level", hdr_uinf_in_jp2h, "uinf inside jp2h (T.800 I.7.3: top level only)",
      CF_JP2, 0, X_LENIENT, "box.nested-top-level", NULL },
    { "uinf.contents", hdr_uinf_url_only, "uinf with a url and no ulst (T.800 I.7.3)", CF_JP2, 0,
      X_LENIENT, "uinf.contents", NULL },
    { "ulst.nu-count", hdr_uinf_nu, "ulst with NU 2 and one UUID (T.800 I.7.3.1)", CF_JP2, 0,
      X_LENIENT, "ulst.nu-count", NULL },
    { "url.version-flags", hdr_uinf_vers, "uinf url with VERS 1 (T.800 I.7.3.2: 0)", CF_JP2, 0,
      X_LENIENT, "url.version-flags", NULL },
    { "url.terminator", hdr_uinf_no_nul, "uinf url LOC without its NUL (T.800 I.7.3.2)", CF_JP2,
      0, X_LENIENT, "url.terminator", NULL },
    { "box.extent", hdr_uinf_after_nul, "uinf url with a byte after LOC's NUL", CF_JP2, 0,
      X_LENIENT, "box.extent", NULL },
    { "url.loc", hdr_uinf_empty_loc, "uinf url with an empty LOC (T.800 I.7.3.2): valid", CF_JP2,
      0, X_VALID, NULL, NULL },
    { "jp2.uuid", hdr_uuid, "uuid with its 16-byte ID and data (T.800 I.7.2): valid", CF_JP2, 0,
      X_VALID, NULL, NULL },
    { "uuid.id", hdr_uuid_short, "uuid of 4 bytes (T.800 I.7.2: a 16-byte ID)", CF_JP2, 0,
      X_LENIENT, "uuid.id", NULL },
    { "ihdr.top-level", hdr_top_ihdr,
      "ihdr at the top level (T.800 I.5.3.1: ignored elsewhere than jp2h): valid", CF_JP2, 0,
      X_VALID, NULL, NULL },
    /* The header boxes. */
    { "colr.method", hdr_second_colr_meth3,
      "second colr with METH 3 (T.800 I.5.3.3: a reader ignores it): valid", CF_JP2, 0, X_VALID,
      NULL, NULL },
    { "colr.icc", hdr_icc_ok, "restricted ICC profile, Monochrome Input (T.800 I.5.3.3): valid",
      CF_JP2, 0, X_VALID, NULL, NULL },
    { "colr.icc-header", hdr_icc_size, "ICC profile size one more than PROFILE", CF_JP2, 0,
      X_LENIENT, "colr.icc-header", NULL },
    { "colr.icc-class", hdr_icc_class, "ICC profile of the Display class ('mntr')", CF_JP2, 0,
      X_LENIENT, "colr.icc-class", NULL },
    { "colr.icc-pcs", hdr_icc_pcs, "ICC profile with the PCS 'Lab ' (T.800 Table I.9: 'XYZ ')",
      CF_JP2, 0, X_LENIENT, "colr.icc-pcs", NULL },
    { "colr.icc-tags", hdr_icc_tags, "Monochrome Input ICC profile without copyrightTag",
      CF_JP2, 0, X_LENIENT, "colr.icc-tags", NULL },
    { "pclr.padding", hdr_pclr_padding, "a 7-bit palette value with its padding bit set "
      "(T.800 I.5.3.4)", CF_JP2, 0, X_LENIENT, "pclr.padding", NULL },
    { "cdef.opacity", hdr_cdef_two_opacities,
      "grey with an opacity for the whole image and one for grey (T.800 I.5.3.6)", CF_JP2, 0,
      X_LENIENT, "cdef.opacity", NULL },
    { "cdef.opacity-signed", hdr_cdef_signed_opacity,
      "opacity from a signed palette column (T.800 Table I.16)", CF_JP2, 0, X_LENIENT,
      "cdef.opacity-signed", NULL },
    { "cdef.pairs", hdr_cdef_two_channels_one_pair, "channels 0 and 1 both Typ 0, Asoc 1", CF_JP2,
      0, X_LENIENT, "cdef.pairs", NULL },
    { "cdef.asoc", hdr_cdef_asoc, "Asoc 2 in a greyscale image (T.800 Table I.18: 1)", CF_JP2, 0,
      X_LENIENT, "cdef.asoc", NULL },
    { "jp2.mct", hdr_mct_srgb, "three components through the RCT, sRGB: valid", CF_JP2, 0,
      X_VALID, NULL, NULL },
    { "jp2.mct-colourspace", hdr_mct_grey,
      "three components through the RCT, greyscale (T.800 I.5.3.6: RGB)", CF_JP2, 0, X_LENIENT,
      "jp2.mct-colourspace", NULL },
    { "jp2.mct-colourspace", hdr_mct_channels,
      "three components through the RCT, sRGB, cdef with G as channel 0 (T.800 I.5.3.6)",
      CF_JP2, 0, X_LENIENT, "jp2.mct-colourspace", NULL },
    { "jp2.ipr-nested", hdr_ipr_in_jp2h, "IPR 1 and an IPR box in jp2h (T.800 I.6): valid",
      CF_JP2, 0, X_VALID, NULL, NULL },
    { "file.ftyp-brand", hdr_jpx_named_jp2,
      "a JPX file listing 'jp2 ', named .jp2: valid, profile invalid (ReadFileType)", CF_JP2, 0,
      X_PROF, "file.ftyp-brand", NULL },
    /* JPX. */
    { "box.nested-top-level", hdr_jpx_jp2h_in_jplh, "jp2h inside a jplh (T.801 M.11.5)", CF_JPX, 0,
      X_LENIENT, "box.nested-top-level", NULL },
    { "box.nested-top-level", hdr_jpx_rreq_in_asoc,
      "rreq inside an asoc (T.801 M.11.1), where InnerPayload has no rreq alternative", CF_JPX, 0,
      X_LENIENT, "decode", NULL },
    { "colr.placement", hdr_jpx_colr_in_jpch, "colr in a jpch (T.801 M.11.7.2)", CF_JPX, 0,
      X_LENIENT, "colr.placement", NULL },
    { "jplh.opct", hdr_jpx_opct, "jplh with an opct: valid", CF_JPX, 0, X_VALID, NULL, NULL },
    { "opct.placement", hdr_jpx_opct_in_jpch, "opct in a jpch (T.801 M.11.7.6: only in jplh)",
      CF_JPX, 0, X_LENIENT, "opct.placement", NULL },
    { "opct.once", hdr_jpx_two_opct, "two opct in a jplh (T.801 M.11.7.6)", CF_JPX, 0, X_LENIENT,
      "opct.once", NULL },
    { "opct.cdef", hdr_jpx_opct_cdef, "opct and cdef in a jplh (T.801 M.11.7)", CF_JPX, 0,
      X_LENIENT, "opct.cdef", NULL },
    { "creg.placement", hdr_jpx_creg_in_jpch, "creg in a jpch (T.801 M.11.7.7: only in jplh)",
      CF_JPX, 0, X_LENIENT, "creg.placement", NULL },
    { "creg.once", hdr_jpx_two_creg, "two creg in a jplh (T.801 M.11.7.7)", CF_JPX, 0, X_LENIENT,
      "creg.once", NULL },
    { "pxfm.once", hdr_jpx_two_pxfm, "two pxfm in jp2h (T.801 M.11.7.8)", CF_JPX, 0, X_LENIENT,
      "pxfm.once", NULL },
    { "jpch.cref", hdr_jpx_cref, "cref in a jpch: valid", CF_JPX, 0, X_VALID, NULL, NULL },
    { "cref.placement", hdr_jpx_top_cref,
      "cref at the top level (T.801 M.11.4: in jpch, jplh or asoc)", CF_JPX, 0, X_LENIENT,
      "cref.placement", NULL },
    { "jpx.comp", hdr_jpx_comp, "comp of copt and inst: valid", CF_JPX, 0, X_VALID, NULL, NULL },
    { "comp.once", hdr_jpx_two_comp, "two comp (T.801 M.11.10)", CF_JPX, 0, X_LENIENT,
      "comp.once", NULL },
    { "comp.copt-first", hdr_jpx_comp_inst_first, "comp without copt first (T.801 M.11.10.1)",
      CF_JPX, 0, X_LENIENT, "comp.copt-first", NULL },
    { "copt.placement", hdr_jpx_copt_in_jpch, "copt in a jpch (T.801 M.11.10.1)", CF_JPX, 0,
      X_LENIENT, "copt.placement", NULL },
    { "jpx.drep", hdr_jpx_drep, "drep with a gtso: valid", CF_JPX, 0, X_VALID, NULL, NULL },
    { "drep.once", hdr_jpx_two_drep, "two drep (T.801 M.11.15)", CF_JPX, 0, X_LENIENT, "drep.once",
      NULL },
    { "gtso.once", hdr_jpx_two_gtso, "two gtso in drep (T.801 M.11.15.1)", CF_JPX, 0, X_LENIENT,
      "gtso.once", NULL },
    { "gtso.placement", hdr_jpx_top_gtso, "gtso at the top level (T.801 M.11.15.1: in drep)",
      CF_JPX, 0, X_LENIENT, "gtso.placement", NULL },
    { "asoc.children", hdr_jpx_asoc_one_child,
      "an asoc of one box inside an asoc (T.801 M.11.11: two or more)", CF_JPX, 0, X_LENIENT,
      "asoc.children", NULL },
    { "lbl.characters", hdr_jpx_lbl_characters, "lbl 'a/b' (T.801 M.11.13: no '/')", CF_JPX, 0,
      X_LENIENT, "lbl.characters", NULL },
    { "box.framing", hdr_jpx_asoc_framing,
      "an asoc inside an asoc, its second box overrunning it", CF_JPX, 0, X_LENIENT,
      "box.framing", NULL },
    { "jpx.colr", hdr_jpx_no_colr, "no jp2h and no cgrp (T.801 M.11.7.2: a colr)", CF_JPX, 0,
      X_LENIENT, "jpx.colr", NULL },
    { "jpx.j2cx", hdr_jpx_j2cx,
      "a j2cx holding a third codestream, three jpch (T.801 M.11.6): valid, profile invalid",
      CF_JPX, 0, X_PROF, "jpx.codestream-count", NULL },
    { "rreq.deprecated-feature", hdr_jpx_deprecated_feature,
      "rreq standard feature 3 (T.801 Table M.14: deprecated)", CF_JPX, 0, X_LENIENT,
      "rreq.deprecated-feature", NULL },
    { "ihdr.c", hdr_jpx_ihdr_c13, "jpch ihdr C 13 (T.801 Table M.19: 0 to 12)", CF_JPX, 0,
      X_LENIENT, "ihdr.c", NULL },
    { "ihdr.c", hdr_jpx_ihdr_c0, "jpch ihdr C 0 for a JPEG 2000 codestream", CF_JPX, 0, X_LENIENT,
      "ihdr.c", NULL },
    { "ihdr.bpcc", hdr_jpx_bpcc_in_jp2h,
      "jpch ihdr BPC 255, bpcc in jp2h (T.801 M.11.5.1: in the ihdr's box)", CF_JPX, 0,
      X_LENIENT, "ihdr.bpcc", NULL },
    { "ihdr.part2-depth", hdr_jpx_part2_depth,
      "Rsiz 0x8100 (Part 2 MCT), jpch BPC 8 for Ssiz 7 (T.801 M.11.5.1, NOTE): valid", CF_JPX, 0,
      X_VALID, NULL, NULL },
    { "jpx.ipr-jplh", hdr_jpx_ipr_in_jplh,
      "jpch ihdr IPR 1 and an IPR box in a jplh (T.800 I.6): valid", CF_JPX, 0, X_VALID, NULL,
      NULL },
    { "jpx.jp2-compatible", hdr_jpx_jp2_compatible, "'jp2 ' in the list (T.801 M.2.1): valid",
      CF_JPX, 0, X_VALID, NULL, NULL },
    { "jp2.one-jp2h", hdr_jpx_jp2_no_jp2h, "'jp2 ' in the list and no jp2h (T.800 I.5.3)", CF_JPX,
      0, X_LENIENT, "jp2.one-jp2h", NULL },
    { "colr.enumcs", hdr_jpx_jp2_cielab,
      "'jp2 ' in the list, jp2h's first colr CIELab (T.801 M.3.6)", CF_JPX, 0, X_LENIENT,
      "colr.enumcs", NULL },
    { "jp2h.position", hdr_jpxb_late_jp2h, "'jpxb', jp2h after the codestreams (T.801 M.9.2.7)",
      CF_JPX, 0, X_LENIENT, "jp2h.position", NULL },
    { "jpxb.header-override", hdr_jpxb_override,
      "'jpxb', the first jpch with its own ihdr (T.801 M.9.2.7)", CF_JPX, 0, X_LENIENT,
      "jpxb.header-override", NULL },
    { "jpxb.colour", hdr_jpxb_colour, "'jpxb', the first layer CMYK (T.801 M.9.2.4)", CF_JPX, 0,
      X_LENIENT, "jpxb.colour", NULL },
    { "jpx.lbl", hdr_jpx_top_lbl, "lbl at the top level (T.801 M.11.13): valid", CF_JPX, 0,
      X_VALID, NULL, NULL },
    { "box.containment", hdr_res_colr, "colr in res (T.800 Figure I.1: in jp2h)", CF_JP2, 0,
      X_LENIENT, "box.containment", NULL },
    { "j2cx.order", hdr_jpx_j2cx_early,
      "a j2cx before a top-level jp2c (T.801 M.11.23: after them)", CF_JPX, 0, X_STD,
      "j2cx.order", NULL },
    { "jpx.jclx", hdr_jpx_jclx,
      "jplh, comp, then an empty jclx at the end (T.801 M.11.21): valid", CF_JPX, 0, X_VALID,
      NULL, NULL },
    { "jclx.headers", hdr_jpx_jclx_alone, "jplh and a jclx, no comp (T.801 M.11.21)", CF_JPX,
      0, X_LENIENT, "jclx.headers", NULL },
    { "jclx.order", hdr_jpx_jclx_early, "a jclx before the top-level jpch (T.801 M.11.6)",
      CF_JPX, 0, X_LENIENT, "jclx.order", NULL },
    { "j2ci.placement", hdr_jpx_j2cx_no_info, "a j2cx without a j2ci (T.801 M.11.24: first)",
      CF_JPX, 0, X_STD, "j2ci.placement", NULL },
    { "j2ci.ncs", hdr_jpx_j2cx_ncs, "j2ci Ncs 2, one codestream (T.801 M.11.23)", CF_JPX, 0,
      X_STD, "j2ci.ncs", NULL },
    { "j2cx.contents", hdr_jpx_j2cx_mdat, "an mdat before the codestream in a j2cx (M.11.23)",
      CF_JPX, 0, X_STD, "j2cx.contents", NULL },
    { "j2ci.ltbl", hdr_jpx_j2cx_ltbl, "two codestreams, Ltbl L 1 (T.801 M.11.24)", CF_JPX, 0,
      X_STD, "j2ci.ltbl", NULL },
    { "jpx.j2cx-ltbl", hdr_jpx_j2cx_two,
      "two codestreams, Ltbl their length, R 0 (T.801 M.11.24): valid, profile invalid", CF_JPX,
      0, X_PROF, "jpx.codestream-count", NULL },
    { "j2cx.top-codestreams", hdr_jpx_j2cx_layers,
      "three top-level jplh, two top-level codestreams and a j2cx (T.801 M.11.23)", CF_JPX, 0,
      X_STD, "j2cx.top-codestreams", NULL },
    { "jlxi.placement", hdr_jpx_jclx_no_info, "a jclx without a jlxi (T.801 M.11.22: first)",
      CF_JPX, 0, X_LENIENT, "jlxi.placement", NULL },
    { "jlxi.counts", hdr_jpx_jclx_counts, "jlxi L 2, one jplh (T.801 M.11.21)", CF_JPX, 0,
      X_LENIENT, "jlxi.counts", NULL },
    { "jlxi.repetition", hdr_jpx_jclx_repetition, "jlxi M 0 with C 0 (T.801 M.11.22)", CF_JPX,
      0, X_LENIENT, "jlxi.repetition", NULL },
    { "jclx.contents", hdr_jpx_jclx_inst_first, "an inst before the jplh of its group (M.11.21)",
      CF_JPX, 0, X_LENIENT, "jclx.contents", NULL },
    { "jlxi.frames", hdr_jpx_jclx_frames, "jlxi T 1, F 0, not in the last jclx (M.11.22)",
      CF_JPX, 0, X_LENIENT, "jlxi.frames", NULL },
    { "grp.placement", hdr_jpx_asoc_grp_first, "an asoc whose first box is a grp (M.11.25)",
      CF_JPX, 0, X_LENIENT, "grp.placement", NULL },
    { "inst.placement", hdr_inst_top, "inst at file level (T.801 M.11.10.2)",
      CF_JPX, 0, X_LENIENT, "inst.placement", NULL },
    { "inst.placement", hdr_inst_jpch, "inst in jpch (T.801 M.11.10.2)",
      CF_JPX, 0, X_LENIENT, "inst.placement", NULL },
    { "inst.placement", hdr_inst_asoc, "inst in asoc (T.801 M.11.10.2)",
      CF_JPX, 0, X_LENIENT, "inst.placement", NULL },
    { "jpx.inst-comp", hdr_inst_comp, "inst in comp (T.801 M.11.10.2): valid",
      CF_JPX, 0, X_VALID, NULL, NULL },
    { "jpx.inst-jclx", hdr_inst_jclx, "inst after jplh in jclx (T.801 M.11.10.2): valid",
      CF_JPX, 0, X_VALID, NULL, NULL },
    { "jp2.inst-opaque", hdr_inst_top, "inst is unknown to JP2 (T.800 I.8): valid",
      CF_JP2, 0, X_VALID, NULL, NULL },
};

/* ------------------------------------------------------------------------ */
/* 5c. Byte-level mutants: signature, length, code, insertion, rreq          */
/* ------------------------------------------------------------------------ */

/* Signature box contents corrupted (LBox/TBox intact): the only invalid
 * preamble the code mutants cannot produce. */
static void emit_signature_mutant(Bytes valid, cf_kind kind, const char *base_name,
                                  const char *companions) {
    Regions rs = walk(&valid);
    Bytes m = bytes_dup(valid);
    char name[256];
    put32(m.data + find_top_box(&rs, &valid, HV_BOX_JP)->payload_start, 0);
    free(rs.r);
    snprintf(name, sizeof name, "%s-sig-bad", base_name);
    emit(m, kind, name, "jP.contents", "signature contents zeroed (T.800 I.5.1)", companions, X_STD,
         "file.signature", NULL);
    free(m.data);
}

/* A wrong length is invalid at both layers where both read it, and a
 * documented leniency (standard-invalid, profile-valid) for the children of
 * a box the profile keeps opaque (children_offset): those of jp2h in a
 * .jpx, and of every superbox in a .jp2. The type rejects it: `decode`. */
static Expect length_expect(const Regions *rs, const Region *r, const Bytes *b, cf_kind kind) {
    const Region *parent;
    int depth = 0, p;
    if (strcmp(r->kind, "LBox") != 0 || r->parent < 0) return X_STD;
    parent = &rs->r[r->parent];
    for (p = parent->parent; p >= 0; p = rs->r[p].parent) depth++;
    return children_offset(be32(b->data + parent->start + 4), depth, profile_layer(kind)) < 0
           ? X_LENIENT : X_STD;
}

static void emit_length_mutants(Bytes valid, cf_kind kind, const char *base_name,
                                const char *companions) {
    Regions rs = walk(&valid);
    int i;
    for (i = 0; i < rs.n; ++i) {
        const Region *r = &rs.r[i];
        Expect expect = length_expect(&rs, r, &valid, kind);
        uint32_t v = r->len_width == 2 ? be16(valid.data + r->len_off)
                                       : be32(valid.data + r->len_off);
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
            emit(m, kind, name, field, "length patched", companions, expect, "decode", NULL);
            free(m.data);
        }
    }
    free(rs.r);
}

/* Patch marker codes in place: the body stays well-formed and only its
 * selector changes. SIZ or SOT out of place fails to decode. An unknown
 * code is skipped by its length at both layers in the main header, and at
 * layer 1 in a tile-part header, whose profile admits PLT alone: where it
 * replaces COD or QCD, the codestream lacks one; where it replaces PLT,
 * the codestream is valid at layer 1. Box-type rules use structured
 * mutants so changing the type does not accidentally remove a different
 * mandatory box. The names give the code in decimal. */
static void code_expect(const Bytes *b, const Region *r, uint32_t value, Expect *expect,
                        const char **reason) {
    uint16_t code = be16(b->data + r->start);
    *expect = X_STD;
    *reason = "decode";
    if (value != UNDEFINED_MARKER) return;
    if (code == HV_COD) *reason = "codestream.one-cod-before-sot";
    if (code == HV_QCD) *reason = "codestream.one-qcd-before-sot";
    if (code == HV_PLT) *expect = X_PROF;
}

static void emit_code_mutants(Bytes valid, cf_kind kind, const char *base_name,
                              const char *companions) {
    Regions rs = walk(&valid);
    int i;
    for (i = 0; i < rs.n; ++i) {
        const Region *r = &rs.r[i];
        static const struct { const char *kind; uint32_t value; const char *note; } patches[] = {
            { "Lxxx", UNDEFINED_MARKER, "code FF70: undefined marker" },
            { "Lxxx", HV_SIZ,           "code FF51: SIZ out of place" },
            { "Lxxx", HV_SOT,           "code FF90: SOT where a segment was" },
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
            {
                Expect expect;
                const char *reason;
                code_expect(&valid, r, patches[k].value, &expect, &reason);
                emit(m, kind, name, field, patches[k].note, companions, expect, reason, NULL);
            }
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
        if (r->len_width == 2)
            put16(m.data + r->len_off, (uint16_t) (be16(valid.data + r->len_off) + n));
        else
            put32(m.data + r->len_off, (uint32_t) (be32(valid.data + r->len_off) + n));
    }
    free(rs.r);
    return m;
}

/* The patch of rule_flst_trailing_byte: a byte after the first flst of
 * the first ftbl. insert_bytes grows the boxes that go on past it; this
 * grows the flst and the boxes that end with it. */
static Bytes flst_trailing_byte(Bytes valid) {
    Regions rs = walk(&valid);
    int ftbl = (int) (find_top_box(&rs, &valid, HV_BOX_FTBL) - rs.r), flst = -1, i;
    size_t at;
    Bytes m;
    for (i = ftbl + 1; i < rs.n && flst < 0; ++i)
        if (rs.r[i].parent == ftbl && be32(valid.data + rs.r[i].start + 4) == HV_BOX_FLST)
            flst = i;
    if (flst < 0) die("flst_trailing_byte: no flst in the first ftbl");
    at = rs.r[flst].end;
    m = insert_bytes(valid, at, "\x00", 1);
    for (i = flst; i >= 0; i = rs.r[i].parent)
        if (rs.r[i].end == at)
            put32(m.data + rs.r[i].len_off, be32(m.data + rs.r[i].len_off) + 1);
    free(rs.r);
    return m;
}

/* T.800 A.4.4 / I.5.4: bytes after EOC inside jp2c. Grow only LBox,
 * not Psot, and preserve any following boxes. Exercise the captured tail
 * and both sides of its corpus bound. */
static void emit_eoc_mutants(Bytes valid, cf_kind kind, const char *base_name,
                              const char *companions) {
    static const unsigned char tail[65] = { 1, 2 };
    static const size_t lengths[] = { 1, 2, 64, 65 };
    Regions rs = walk(&valid);
    const Region *box = find_top_box(&rs, &valid, HV_BOX_JP2C);
    size_t start = box->start, end = box->end, k;
    if (end != rs.eoc_end) die("EOC mutants: base jp2c does not end at EOC");
    free(rs.r);
    for (k = 0; k < sizeof lengths / sizeof *lengths; k++) {
        size_t n = lengths[k];
        Bytes m = insert_bytes(valid, end, tail, n);
        char name[256];
        put32(m.data + start, be32(valid.data + start) + (uint32_t)n);
        snprintf(name, sizeof name, "%s-after-eoc-%zu", base_name, n);
        emit(m, kind, name, "codestream.extent",
             "bytes after EOC inside jp2c (T.800 A.4.4, I.5.4)", companions,
             X_STD, n > 64 ? "decode" : "codestream.extent", NULL);
        free(m.data);
    }
}

/* Insertions the structured mutants cannot make: codes placed before the
 * first SOT or in the tile-part header. PPT and SOP out of place, FF30
 * read with a length and FF00 fail to decode. A code T.800 does not define
 * is skipped by its length (A.1), at both layers in the main header, as
 * the server does, and at layer 1 in a tile-part header. FF30, a marker
 * without a segment (A.1.3), is skipped at layer 1; the profile excludes
 * it. An Iplt longer than the model's ten bytes, which T.800 allows, is
 * past a corpus bound (j2k-headers.asn1): lib/test covers it. */
static void emit_insertion_mutants(Bytes valid, cf_kind kind, const char *base_name,
                                   const char *companions) {
    static const struct {
        const char *name, *bytes;
        size_t n;
        const char *note;
        Expect expect;
    } main_codes[] = {
        { "ppt", "\xFF\x61\x00\x03\x00", 5, "PPT, a tile-part header marker, in the main header",
          X_STD },
        { "sop", "\xFF\x91\x00\x04\x00\x00", 6, "SOP, a packet marker, in the main header", X_STD },
        { "ff30-length", "\xFF\x30\x00\x02", 4,
          "FF30 read with a length: FF30 to FF3F have no segment (A.1.3)", X_STD },
        { "ff00", "\xFF\x00\x00\x02", 4, "FF00, not a marker, in the main header", X_STD },
        { "unknown", "\xFF\x70\x00\x04\x00\x00", 6,
          "FF70, a code T.800 does not define, in the main header: skipped (A.1)", X_VALID },
        { "ff30", "\xFF\x30", 2,
          "FF30, a marker without a segment, in the main header: skipped (A.1.3)", X_PROF },
    };
    Regions rs = walk(&valid);
    size_t sot = 0, plt = 0, plt_segment = 0, k;
    int i, tp = -1;
    for (i = 0; i < rs.n && tp < 0; ++i)
        if (strcmp(rs.r[i].kind, "Psot") == 0) { tp = i; sot = rs.r[i].start; }
    for (i = tp + 1; i < rs.n && plt == 0; ++i)
        if (rs.r[i].parent == tp && be16(valid.data + rs.r[i].start) == HV_PLT) {
            plt = rs.r[i].payload_start + 1;              /* first Iplt, after Zplt */
            plt_segment = rs.r[i].start;
        }
    free(rs.r);
    if (tp < 0 || plt == 0) die("insertion mutants: no tile-part with PLT");
    for (k = 0; k < sizeof main_codes / sizeof *main_codes; ++k) {
        Bytes m = insert_bytes(valid, sot, main_codes[k].bytes, main_codes[k].n);
        char name[256];
        snprintf(name, sizeof name, "%s-main-%s", base_name, main_codes[k].name);
        emit(m, kind, name, "main.marker-code", main_codes[k].note, companions, main_codes[k].expect,
             main_codes[k].expect == X_VALID ? NULL : "decode", NULL);
        free(m.data);
    }
    {
        /* TLM, a main-header marker (Table A.2), in the tile-part header. */
        Bytes m = insert_bytes(valid, plt_segment, "\xFF\x55\x00\x04\x00\x00", 6);
        char name[256];
        snprintf(name, sizeof name, "%s-tile-tlm", base_name);
        emit(m, kind, name, "tile.marker-code",
             "TLM, a main-header marker, in the tile-part header", companions, X_STD, "decode", NULL);
        free(m.data);
    }
    {
        /* An unknown code and FF30 in the tile-part header: skipped at
         * layer 1 (A.1, A.1.3); TileSegment-Profile admits PLT alone. */
        static const struct { const char *name, *bytes; size_t n; const char *note; } tile[] = {
            { "unknown", "\xFF\x70\x00\x04\x00\x00", 6,
              "FF70, a code T.800 does not define, in the tile-part header: skipped (A.1)" },
            { "ff30", "\xFF\x30", 2,
              "FF30, a marker without a segment, in the tile-part header: skipped (A.1.3)" },
        };
        for (k = 0; k < sizeof tile / sizeof *tile; ++k) {
            Bytes m = insert_bytes(valid, plt_segment, tile[k].bytes, tile[k].n);
            char name[256];
            snprintf(name, sizeof name, "%s-tile-%s", base_name, tile[k].name);
            emit(m, kind, name, "tile.marker-code", tile[k].note, companions, X_PROF, "decode",
                 NULL);
            free(m.data);
        }
    }
}

/* Reader Requirements counts and extent the typed mutants cannot break
 * (NSF, NVF and ML are determinants): NSF and NVF one too high, and a byte
 * after the contents. Invalid at layer 1; the profile keeps rreq opaque. */
static void emit_rreq_mutants(Bytes valid, const char *base_name, const char *companions) {
    Regions rs = walk(&valid);
    const Region *box = find_top_box(&rs, &valid, HV_BOX_RREQ);
    size_t rreq = box->start, payload = box->payload_start, nsf, nvf;
    unsigned ml;
    char name[256];
    Bytes m;
    free(rs.r);
    ml = valid.data[payload];
    nsf = payload + 1 + 2 * ml;
    nvf = nsf + 2 + (2 + ml) * be16(valid.data + nsf);
    m = bytes_dup(valid);
    put16(m.data + nsf, (uint16_t) (be16(m.data + nsf) + 1));
    snprintf(name, sizeof name, "%s-header-rreq.nsf", base_name);
    emit(m, CF_JPX, name, "rreq.nsf", "NSF one more than the standard features", companions, X_LENIENT,
         "decode", NULL);
    free(m.data);
    m = bytes_dup(valid);
    put16(m.data + nvf, (uint16_t) (be16(m.data + nvf) + 1));
    snprintf(name, sizeof name, "%s-header-rreq.nvf", base_name);
    emit(m, CF_JPX, name, "rreq.nvf", "NVF 1 with no vendor feature", companions, X_LENIENT,
         "decode", NULL);
    free(m.data);
    m = insert_bytes(valid, nvf + 2, "\x00", 1);                 /* at the end of the box */
    put32(m.data + rreq, be32(m.data + rreq) + 1);
    snprintf(name, sizeof name, "%s-header-rreq.extent", base_name);
    emit(m, CF_JPX, name, "rreq.extent", "a byte after the vendor features, inside the box", companions,
         X_LENIENT, "rreq.extent", NULL);
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

    emit(valid, base.kind, base.name, NULL, "base", companions, X_VALID, NULL, NULL);
    emit_signature_mutant(valid, base.kind, base.name, companions);
    emit_length_mutants(valid, base.kind, base.name, companions);
    emit_code_mutants(valid, base.kind, base.name, companions);
    if (base.jp2c_box >= 0) {
        emit_insertion_mutants(valid, base.kind, base.name, companions);
        emit_eoc_mutants(valid, base.kind, base.name, companions);
    }
    if (base.kind == CF_JPX && base.jp2c_box >= 0)
        emit_rreq_mutants(valid, base.name, companions);

    if (base.jp2c_box >= 0) {
        int has_precincts = cod_of(base.file, base.jp2c_box)->scod.customPrecincts;
        for (i = 0; i < sizeof field_mutants / sizeof *field_mutants; ++i) {
            const FieldMutant *fm = &field_mutants[i];
            Bytes m;
            if (fm->needs_precincts != has_precincts) continue;
            if (fm->only_kind && fm->only_kind != (int) base.kind) continue;
            *work = *base.file;
            fm->set(work, base.jp2c_box, fm->value);
            sync_headers(work);
            m = encode_family(work, 0);
            snprintf(name, sizeof name, "%s-%s-%lld", base.name, fm->field, (long long) fm->value);
            emit(m, base.kind, name, fm->field, fm->note, companions, fm->expect, fm->reason,
                 NULL);
            free(m.data);
        }
        for (i = 0; i < sizeof rule_mutants / sizeof *rule_mutants; ++i) {
            const RuleMutant *rm = &rule_mutants[i];
            Bytes m;
            if (rm->only_kind && rm->only_kind != (int) base.kind) continue;
            if (rm->needs_precincts != has_precincts) continue;
            *work = *base.file;
            wire_patch = NULL;
            retypes = 0;
            rm->apply(work, base.jp2c_box);
            sync_headers(work);
            m = encode_family(work, 0);
            if (wire_patch != NULL) {
                Bytes patched = wire_patch(m);
                free(m.data);
                m = patched;
            }
            snprintf(name, sizeof name, "%s-rule-%s-%zu", base.name, rm->name, i);
            emit(m, base.kind, name, rm->name, rm->note, companions, rm->expect, rm->reason,
                 rm->profile_reason);
            free(m.data);
        }
        for (i = 0; i < sizeof header_mutants / sizeof *header_mutants; ++i) {
            const RuleMutant *rm = &header_mutants[i];
            Bytes m;
            if (rm->only_kind != (int) base.kind || has_precincts) continue;
            *work = *base.file;
            wire_patch = NULL;
            retypes = 0;
            rm->apply(work, base.jp2c_box);
            m = encode_family(work, 0);
            if (wire_patch != NULL) {
                Bytes patched = wire_patch(m);
                free(m.data);
                m = patched;
            }
            snprintf(name, sizeof name, "%s-header-%s-%zu", base.name, rm->name, i);
            emit(m, base.kind, name, rm->name, rm->note, companions, rm->expect, rm->reason,
                 rm->profile_reason);
            free(m.data);
        }
    } else {
        for (i = 0; i < sizeof linked_rule_mutants / sizeof *linked_rule_mutants; ++i) {
            const RuleMutant *rm = &linked_rule_mutants[i];
            Bytes m;
            *work = *base.file;
            wire_patch = NULL;
            retypes = 0;
            rm->apply(work, -1);
            m = encode_family(work, 0);
            if (wire_patch != NULL) {
                Bytes patched = wire_patch(m);
                free(m.data);
                m = patched;
            }
            snprintf(name, sizeof name, "%s-rule-%s-%zu", base.name, rm->name, i);
            emit(m, base.kind, name, rm->name, rm->note, companions, rm->expect, rm->reason,
                 rm->profile_reason);
            free(m.data);
        }
    }
    free(work);
    free(valid.data);
    free(base.file);
}

/* The encoder writes MAPPING_OTHER_BOX ('abcd') for an `other` box. Patch
 * its TBox on the wire to exercise the decoder's full unknown-type
 * fallback. */
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
    patch_other_type(&b, 0, MAPPING_OTHER_BOX, be32((const unsigned char *) "wxyz"));
    emit(b, CF_JP2, "jp2-unknown-top-wxyz", "file.unknown-box",
         "unlisted top-level box type: valid and skipped", NULL, X_VALID, NULL, NULL);
    patch_other_type(&b, 0, be32((const unsigned char *) "wxyz"), 0xF0E1D2C3u);
    emit(b, CF_JP2, "jp2-unknown-top-binary", "file.unknown-box",
         "unlisted non-ASCII box type: valid and skipped", NULL, X_VALID, NULL, NULL);
    free(b.data);
    free(base.file);

    base = base_jpx_embedded();
    rule_unknown_box(base.file, -1);
    b = encode_family(base.file, 1);
    patch_other_type(&b, 0, MAPPING_OTHER_BOX, be32((const unsigned char *) "wxyz"));
    emit(b, CF_JPX, "jpx-unknown-top-wxyz", "file.unknown-box",
         "unlisted top-level box type: valid and skipped", NULL, X_VALID, NULL, NULL);
    free(b.data);
    free(base.file);

    /* flst and url at the top level, where TopPayload has no alternative
     * for them: `decode`, at both layers for a .jpx (ReadJPX rejects them;
     * a .jp2's profile keeps them opaque). A JP2 file's model (Jp2File)
     * keeps them opaque: T.800 does not define flst, and puts url in uinf
     * (box.url-placement). One fragment of this file (OFF 12, LEN 1); a
     * link to a .jp2. */
    {
        static const struct {
            const char *name, *type, *payload;
            size_t n;
            cf_kind kind;
            Expect expect;
            const char *note;
        } top[] = {
            { "jp2-top-flst", "flst", "\x00\x01\x00\x00\x00\x00\x00\x00\x00\x0C"
              "\x00\x00\x00\x01\x00\x00", 16, CF_JP2, X_VALID,
              "flst at the top level of a JP2 file: a box T.800 does not define (I.8)" },
            { "jpx-top-flst", "flst", "\x00\x01\x00\x00\x00\x00\x00\x00\x00\x0C"
              "\x00\x00\x00\x01\x00\x00", 16, CF_JPX, X_STD,
              "flst at the top level (T.801 M.11.3: in ftbl)" },
            { "jpx-top-url", "url ", "\x00\x00\x00\x00""file:///a.jp2", 18, CF_JPX, X_STD,
              "url at the top level (T.801 M.11.2: in dtbl)" },
            { "jp2-top-url", "url ", "\x00\x00\x00\x00""file:///a.jp2", 18, CF_JP2, X_LENIENT,
              "url at the top level (T.800 I.2.2, Figure I.1: in uinf)" },
        };
        size_t t;
        for (t = 0; t < sizeof top / sizeof top[0]; ++t) {
            TopBox *other;
            base = top[t].kind == CF_JP2 ? base_jp2(0, 0) : base_jpx_embedded();
            other = &base.file->boxes.arr[base.file->boxes.nCount++];
            other->payload.kind = TopPayload_other_PRESENT;
            OCTETS(other->payload.u.other.data, top[t].payload, top[t].n);
            b = encode_family(base.file, 1);
            patch_other_type(&b, 0, MAPPING_OTHER_BOX, be32((const unsigned char *) top[t].type));
            emit(b, top[t].kind, top[t].name,
                 top[t].type[0] == 'f' ? "box.flst-placement" : "box.url-placement", top[t].note,
                 NULL, top[t].expect,
                 top[t].expect == X_VALID ? NULL
                 : top[t].kind == CF_JP2 ? "box.url-placement" : "decode", NULL);
            free(b.data);
            free(base.file);
        }
    }

    base = base_jpx_embedded();
    superbox = jpch_of(base.file);
    child = &superbox->children.arr[superbox->children.nCount++];
    child->payload.kind = InnerPayload_other_PRESENT;
    OCTETS(child->payload.u.other.data, "\x01", 1);
    b = encode_family(base.file, 1);
    patch_other_type(&b, 1, MAPPING_OTHER_BOX, be32((const unsigned char *) "wxyz"));
    emit(b, CF_JPX, "jpx-unknown-inner-wxyz", "file.unknown-box",
         "unlisted box inside jpch: valid and skipped", NULL, X_VALID, NULL, NULL);
    free(b.data);
    free(base.file);
}

/* The boxes T.801 keeps at the top level (M.11.2, M.11.3, M.11.6), each
 * inside the first jpch of an embedded JPX with a payload valid for its
 * type, so that only its placement is wrong: box.nested-top-level at both
 * layers, as the server walks jpch. */
static void emit_nested_boxes(void) {
    static const unsigned char flst[] = {       /* one fragment of this file */
        0x00, 0x00, 0x00, 0x18, 'f', 'l', 's', 't', 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,           /* OFF */
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };                     /* LEN, DR */
    static const struct {
        const char *name;
        int kind;
        const unsigned char *payload;
        size_t n;
        const char *note;
    } nested[] = {
        { "jpx-nested-jpch", InnerPayload_jpch_PRESENT, NULL, 0,
          "empty jpch inside jpch (T.801 M.11.6: top level only)" },
        { "jpx-nested-ftbl", InnerPayload_ftbl_PRESENT, flst, sizeof flst,
          "ftbl holding one flst inside jpch (T.801 M.11.3: top level only)" },
        { "jpx-nested-dtbl", InnerPayload_dtbl_PRESENT, (const unsigned char *) "\x00\x00", 2,
          "dtbl with NDR 0 inside jpch (T.801 M.11.2: top level only)" },
    };
    size_t i;
    for (i = 0; i < sizeof nested / sizeof *nested; ++i) {
        Base base = base_jpx_embedded();
        Superbox *superbox = jpch_of(base.file);
        InnerBox *child = &superbox->children.arr[superbox->children.nCount++];
        OpaqueBox *box = nested[i].kind == InnerPayload_jpch_PRESENT ? &child->payload.u.jpch
                       : nested[i].kind == InnerPayload_ftbl_PRESENT ? &child->payload.u.ftbl
                       : &child->payload.u.dtbl;
        Bytes b;
        child->payload.kind = nested[i].kind;
        box->data.nCount = (int) nested[i].n;
        if (nested[i].n > 0) memcpy(box->data.arr, nested[i].payload, nested[i].n);
        b = encode_family(base.file, 1);
        emit(b, CF_JPX, nested[i].name, "box.nested-top-level", nested[i].note, NULL, X_STD,
             "box.nested-top-level", "box.nested-top-level");
        free(b.data);
        free(base.file);
    }
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
    OCTETS(child->payload.u.lbl.data, "test", 4); /* not NUL-terminated (M.11.13) */
    child = &box->payload.u.asoc.children.arr[1];
    child->payload.kind = InnerPayload_xml_PRESENT;
    OCTETS(child->payload.u.xml.data, "<meta/>", 7);
    b = encode_family(base.file, 1);
    emit(b, CF_JPX, "jpx-asoc", "asoc.opaque",
         "T.801 M.11.11: label associated with XML; profile preserves opaque contents", NULL,
         X_VALID, NULL, NULL);

    mutant = bytes_dup(b);
    put32(mutant.data + offset + 8, 7);
    emit(mutant, CF_JPX, "jpx-asoc-child-length", "asoc.opaque",
         "inner LBox below 8: standard invalid, profile preserves opaque contents", NULL,
         X_LENIENT, "decode", NULL);
    free(mutant.data);
    mutant = bytes_dup(b);
    put32(mutant.data + offset, (uint32_t) (b.len - offset + 1));
    emit(mutant, CF_JPX, "jpx-asoc-outer-length", "asoc.outer-length",
         "outer LBox exceeds file: invalid at both layers", NULL, X_STD, "decode", NULL);
    free(mutant.data);
    free(b.data);

    box->payload.u.asoc.children.nCount = 1;
    b = encode_family(base.file, 0);
    emit(b, CF_JPX, "jpx-asoc-one-child", "asoc.opaque",
         "T.801 M.11.11 requires at least two children; profile preserves opaque contents",
         NULL, X_LENIENT, "decode", NULL);
    free(b.data);

    /* The label, and an association of a label and XML (M.11.11 lets
     * associations nest; the model keeps the inner one opaque). */
    {
        static const unsigned char inner[] = {
            0x00, 0x00, 0x00, 0x0C, 'l', 'b', 'l', ' ', 't', 'e', 's', 't',
            0x00, 0x00, 0x00, 0x0F, 'x', 'm', 'l', ' ', '<', 'm', 'e', 't', 'a', '/', '>' };
        box->payload.u.asoc.children.nCount = 2;
        child = &box->payload.u.asoc.children.arr[1];
        memset(child, 0, sizeof *child);
        child->payload.kind = InnerPayload_asoc_PRESENT;
        OCTETS(child->payload.u.asoc.data, inner, sizeof inner);
    }
    b = encode_family(base.file, 1);
    emit(b, CF_JPX, "jpx-asoc-nested", "asoc.nested",
         "T.801 M.11.11: an association inside an association; valid, contents opaque", NULL,
         X_VALID, NULL, NULL);
    free(b.data);
    free(base.file);
}

/* These named C rules remain in the reader. Check their overlapping cases
 * against the subtype validators that the corpus decoder now calls. */
static void check_profile_rule_parity(void) {
    Base base = base_jp2(0, 0);
    Siz *siz = &base.file->boxes.arr[base.jp2c_box].payload.u.jp2c.siz.body;
    const hv_siz view = cf_siz_view(siz);
    asn1SccUint *origins[] = {&siz->fixed.xosiz, &siz->fixed.yosiz};
    asn1SccUint *sampling[] = {&siz->components.arr[0].xrsiz,
                            &siz->components.arr[0].yrsiz};
    Cod cod;
    DataEntryUrl url;
    FragmentList flst;
    size_t i;
    int err = 0;

    if (!Siz_Profile_IsConstraintValid(siz, &err) || hv_rule_siz(&view, NULL, 1) != NULL)
        die("SIZ profile rules disagree on the base");
    for (i = 0; i < sizeof origins / sizeof *origins; i++) {
        *origins[i] = 1;
        if (Siz_Profile_IsConstraintValid(siz, &err) ||
            hv_rule_siz(&view, NULL, 1) == NULL)
            die("SIZ origin rules disagree");
        *origins[i] = 0;
    }
    for (i = 0; i < sizeof sampling / sizeof *sampling; i++) {
        *sampling[i] = 2;
        if (Siz_Profile_IsConstraintValid(siz, &err) ||
            strcmp(hv_rule_siz(&view, NULL, 1), "siz.component-sampling") != 0)
            die("SIZ sampling rules disagree");
        *sampling[i] = 1;
    }
    free(base.file);

    Cod_Initialize(&cod);                   /* layer 1 minima: no precincts, 1 layer */
    if (!Cod_Profile_IsConstraintValid(&cod, &err) ||
        hv_rule_cod(&cod.scod, &cod.spcod, 1) != NULL)
        die("COD profile rules disagree on the base");
    cod.scod.sopMarkers = TRUE;
    if (Cod_Profile_IsConstraintValid(&cod, &err) ||
        strcmp(hv_rule_cod(&cod.scod, &cod.spcod, 1), "cod.sop-markers") != 0 ||
        hv_rule_cod(&cod.scod, &cod.spcod, 0) != NULL)
        die("COD SOP rules disagree");

    /* VERS and FLAG: layer 1 (UrlHeader). */
    DataEntryUrl_Initialize(&url);
    url.header.vers = 0;
    url.header.flag = 0;
    strcpy(url.loc, "file://a.jp2");
    if (!DataEntryUrl_Profile_IsConstraintValid(&url, &err) ||
        hv_rule_url(url.header.vers, url.header.flag, (const uint8_t *)url.loc,
                    strlen(url.loc) + 1) != NULL)
        die("URL profile rules disagree on the base");
    url.header.vers = 1;
    if (DataEntryUrl_IsConstraintValid(&url, &err) ||
        strcmp(hv_rule_url(url.header.vers, url.header.flag, (const uint8_t *)url.loc,
                           strlen(url.loc) + 1), "url.version-flags") != 0)
        die("URL version rules disagree");
    url.header.vers = 0;
    url.header.flag = 1;
    if (DataEntryUrl_IsConstraintValid(&url, &err) ||
        strcmp(hv_rule_url(url.header.vers, url.header.flag, (const uint8_t *)url.loc,
                           strlen(url.loc) + 1), "url.version-flags") != 0)
        die("URL flag rules disagree");
    url.header.flag = 0;
    strcpy(url.loc, "file://");
    if (DataEntryUrl_Profile_IsConstraintValid(&url, &err) ||
        strcmp(hv_rule_url(url.header.vers, url.header.flag, (const uint8_t *)url.loc,
                           strlen(url.loc) + 1), "url.length") != 0)
        die("URL length rules disagree");

    FragmentList_Initialize(&flst);
    flst.nf = 1;
    flst.fragments.nCount = 1;
    flst.fragments.arr[0].off = 12;
    flst.fragments.arr[0].len = 1;
    flst.fragments.arr[0].dr = 1;
    if (!FragmentList_Profile_IsConstraintValid(&flst, &err) ||
        hv_rule_flst(flst.nf, flst.fragments.nCount) != NULL)
        die("fragment-list profile rules disagree on the base");
    flst.nf = 0;
    if (FragmentList_Profile_IsConstraintValid(&flst, &err) ||
        strcmp(hv_rule_flst(flst.nf, flst.fragments.nCount), "flst.one-fragment") != 0)
        die("fragment count rules disagree");
    flst.nf = 1;
    flst.fragments.nCount = 0;
    if (FragmentList_Profile_IsConstraintValid(&flst, &err) ||
        strcmp(hv_rule_flst(flst.nf, flst.fragments.nCount), "flst.one-fragment") != 0)
        die("fragment-list length rules disagree");
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
    fprintf(manifest,
            "file\tkind\tstandard\tprofile\treason\tfield\tnote\tcompanions\tprofile_reason\n");

    encode_buffer = xcalloc(FILE_BUFFER_BYTES);
    label_init();

    /* Sanity: the box-type constants in the model are the four-cc values. */
    if (be32((const unsigned char *) "jp2c") != HV_BOX_JP2C || be32((const unsigned char *) "flst") != HV_BOX_FLST)
        die("box type constants do not match four-cc values");
    check_profile_rule_parity();

    run_base(base_jp2(0, 0), NULL);
    run_base(base_jp2(1, 1), NULL);
    run_base(base_jpx_embedded(), NULL);
    emit_unknown_box_types();
    emit_nested_boxes();
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
            label_companion(urls[i], off[i], len[i]);
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
            label_companion(graph_urls[1], off[1], len[1]);
            free(rs.r);
            free(b.data);
            free(frame.file);
            for (graph = 0; graph < 3; ++graph) {
                Base linked = base_jpx_linked(graph_urls, off, len, 2);
                for (stream = 0; stream < 2; ++stream) {
                    Fragment *fragment = &flst_of(linked.file, stream)->fragments.arr[0];
                    int reference = references[graph][stream];
                    fragment->dr = reference;
                    fragment->off = off[reference - 1];
                    fragment->len = len[reference - 1];
                }
                b = encode_family(linked.file, 1);
                emit(b, CF_JPX, names[graph], "jpx.reference-order",
                     "T.801 M.11.2/M.11.3.1: distinct companions, DR mapping independent of "
                     "codestream order",
                     "jpx-linked-frame1.jp2,jpx-graph-frame2.jp2", X_VALID, NULL, NULL);
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
