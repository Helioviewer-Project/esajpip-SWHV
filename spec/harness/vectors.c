/* vectors.c — builds the JPEG 2000 test-vector corpus from the ACN model.
 *
 * Usage:  vectors <output-directory>
 *
 * Produces complete .jp2 / .jpx files plus manifest.tsv. Every vector is
 * labelled at both layers by decoding it with the generated decoders, running
 * the generated constraint checkers, and applying crossfield.c. Nothing is
 * labelled by hand.
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

/* Marker codes and box types (decimal, as in the .asn1). */
enum {
    MC_SOC = 65359, MC_SIZ = 65361, MC_COD = 65362, MC_QCD = 65372,
    MC_PLT = 65368, MC_COM = 65380, MC_SOT = 65424, MC_SOD = 65427, MC_EOC = 65497
};
enum {
    BT_JP = 1783636000u, BT_FTYP = 1718909296u, BT_JP2H = 1785737832u,
    BT_IHDR = 1768449138u, BT_COLR = 1668246642u, BT_JP2C = 1785737827u,
    BT_JPCH = 1785750376u, BT_FTBL = 1718903404u, BT_FLST = 1718383476u,
    BT_DTBL = 1685348972u, BT_URL = 1970433056u
};

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
    return t == BT_JP2H || t == BT_JPCH || t == BT_FTBL || t == BT_DTBL ||
           t == 1634955107u /* asoc */ || t == 1785752680u /* jplh */ || t == 1969843814u /* uinf */;
}

static int walk_codestream(Regions *rs, const Bytes *b, size_t pos, size_t end, int parent) {
    size_t soc = pos;
    if (end - pos < 2 || be16(b->data + pos) != MC_SOC) return 0;
    pos += 2;
    while (pos + 2 <= end) {
        uint16_t code = be16(b->data + pos);
        if (code == MC_EOC) {
            if (rs->soc_off == 0 && rs->eoc_end == 0) { rs->soc_off = soc; rs->eoc_end = pos + 2; }
            return 1;
        }
        if (code == MC_SOT) {
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
                if (c == MC_SOD) break;
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
        if (t == BT_JP2C) {
            if (!walk_codestream(rs, b, pos + 8, box_end, idx)) return 0;
        } else if (is_superbox(t)) {
            size_t child_start = pos + 8 + (t == BT_DTBL ? 2 : 0);
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
    c->spcod.exist.precincts = custom_precincts;
    if (custom_precincts) {
        c->spcod.precincts.lowest.ppx = 15;
        c->spcod.precincts.lowest.ppy = 15;
        c->spcod.precincts.higher.nCount = levels;
        for (i = 0; i < levels; ++i) {
            c->spcod.precincts.higher.arr[i].ppx = 15;
            c->spcod.precincts.higher.arr[i].ppy = 15;
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
        ts->code = MC_PLT;
        ts->body.kind = TileBody_plt_PRESENT;
        plt = &ts->body.u.plt.body;
        plt->zplt = 0;
        plt->entries.nCount = packets;
        for (i = 0; i < packets; ++i) {
            Iplt *e = &plt->entries.arr[i];
            memset(e, 0, sizeof *e);
            e->b0.more = 0;
            e->b0.bits = 1;                    /* packet length 1 */
        }
    }
    tp->rest.data.nCount = packets;
    for (i = 0; i < packets; ++i) tp->rest.data.arr[i] = 0x00;
}

static void build_codestream(Codestream *cs, int width, int height, int levels,
                             int custom_precincts, int with_plt) {
    MainSegment *seg;
    INIT(Codestream)(cs);
    cs->soc = MC_SOC;
    cs->sizCode = MC_SIZ;
    build_siz(&cs->siz.body, width, height);
    cs->segments.nCount = 3;

    seg = &cs->segments.arr[0];
    seg->code = MC_COD;
    seg->body.kind = MainBody_cod_PRESENT;
    build_cod(&seg->body.u.cod.body, levels, custom_precincts);

    seg = &cs->segments.arr[1];
    seg->code = MC_QCD;
    seg->body.kind = MainBody_qcd_PRESENT;
    seg->body.u.qcd.body.sqcd = 0x40;          /* 2 guard bits, no quantization */
    {
        /* one SPqcd byte per sub-band: 3 * levels + 1 */
        int n = 3 * levels + 1, i;
        seg->body.u.qcd.body.spqcd.nCount = n;
        for (i = 0; i < n; ++i) seg->body.u.qcd.body.spqcd.arr[i] = 0x40;
    }

    seg = &cs->segments.arr[2];
    seg->code = MC_SOT;
    seg->body.kind = MainBody_tilePart_PRESENT;
    build_tile_part(&seg->body.u.tilePart, levels, with_plt);
}

static void add_opaque_box(Jp2Family *f, uint32_t type, const void *data, size_t n) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->tbox = type;
    switch (type) {
        case BT_JP:   b->payload.kind = TopPayload_jP_PRESENT;   OCTETS(b->payload.u.jP.data, data, n); break;
        case BT_FTYP: b->payload.kind = TopPayload_ftyp_PRESENT; OCTETS(b->payload.u.ftyp.data, data, n); break;
        default: die("add_opaque_box: unsupported type");
    }
}

static void add_signature_and_ftyp(Jp2Family *f, const char *brand) {
    unsigned char ftyp[12];
    add_opaque_box(f, BT_JP, "\x0D\x0A\x87\x0A", 4);
    memcpy(ftyp, brand, 4);
    put32(ftyp + 4, 0);
    memcpy(ftyp + 8, brand, 4);
    add_opaque_box(f, BT_FTYP, ftyp, 12);
}

static void add_jp2h(Jp2Family *f, int width, int height) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    InnerBox *c;
    unsigned char ihdr[14], colr[7];
    b->tbox = BT_JP2H;
    b->payload.kind = TopPayload_jp2h_PRESENT;
    b->payload.u.jp2h.children.nCount = 2;
    put32(ihdr, (uint32_t) height); put32(ihdr + 4, (uint32_t) width);
    put16(ihdr + 8, 1); ihdr[10] = 7; ihdr[11] = 7; ihdr[12] = 0; ihdr[13] = 0;
    c = &b->payload.u.jp2h.children.arr[0];
    c->tbox = BT_IHDR; c->payload.kind = InnerPayload_ihdr_PRESENT;
    OCTETS(c->payload.u.ihdr.data, ihdr, sizeof ihdr);
    colr[0] = 1; colr[1] = 0; colr[2] = 0; put32(colr + 3, 17);   /* greyscale */
    c = &b->payload.u.jp2h.children.arr[1];
    c->tbox = BT_COLR; c->payload.kind = InnerPayload_colr_PRESENT;
    OCTETS(c->payload.u.colr.data, colr, sizeof colr);
}

static Codestream *add_jp2c(Jp2Family *f, int width, int height, int levels,
                            int custom_precincts, int with_plt) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->tbox = BT_JP2C;
    b->payload.kind = TopPayload_jp2c_PRESENT;
    build_codestream(&b->payload.u.jp2c, width, height, levels, custom_precincts, with_plt);
    return &b->payload.u.jp2c;
}

static void add_jpch(Jp2Family *f) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    b->tbox = BT_JPCH;
    b->payload.kind = TopPayload_jpch_PRESENT;
    b->payload.u.jpch.children.nCount = 0;
}

static void add_ftbl(Jp2Family *f, uint64_t off, uint32_t len, int dr) {
    TopBox *b = &f->boxes.arr[f->boxes.nCount++];
    InnerBox *c;
    b->tbox = BT_FTBL;
    b->payload.kind = TopPayload_ftbl_PRESENT;
    b->payload.u.ftbl.children.nCount = 1;
    c = &b->payload.u.ftbl.children.arr[0];
    c->tbox = BT_FLST;
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
    b->tbox = BT_DTBL;
    b->payload.kind = TopPayload_dtbl_PRESENT;
    b->payload.u.dtbl.ndr = n;
    b->payload.u.dtbl.references.nCount = n;
    for (i = 0; i < n; ++i) {
        InnerBox *c = &b->payload.u.dtbl.references.arr[i];
        c->tbox = BT_URL;
        c->payload.kind = InnerPayload_url_PRESENT;
        c->payload.u.url.vers = 0;
        c->payload.u.url.flag = 0;
        strncpy((char *) c->payload.u.url.loc, urls[i], sizeof c->payload.u.url.loc - 1);
    }
}

/* Canonical files. Sizes 4x4 leave room for origin/tile mutants. */
#define BASE_W 4
#define BASE_H 4

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
    INIT(Jp2Family)(f);
    f->boxes.nCount = 0;
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
    Base b = { "jpx-embedded", CF_JPX, new_family(), 4 };
    add_signature_and_ftyp(b.file, "jpx ");
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

static Jp2Family *dec_family;            /* reused decode targets */
static Jp2File_Profile *dec_jp2;
static JpxFile_Profile *dec_jpx;

static Label label(Bytes b, cf_kind kind) {
    Label l = { 0, 0, NULL, NULL };
    BitStream bs;
    int err = 0;
    const char *r;

    BitStream_Init(&bs, b.data, (long) b.len);
    INIT(Jp2Family)(dec_family);
    if (!DEC(Jp2Family)(dec_family, &bs, &err)) l.std_reason = "decode";
    else if (!VALID(Jp2Family)(dec_family, &err)) l.std_reason = "constraint";
    else if ((r = cf_check_family(dec_family, CF_STANDARD, kind)) != NULL) l.std_reason = r;
    else l.std_ok = 1;

    BitStream_Init(&bs, b.data, (long) b.len);
    if (kind == CF_JP2) {
        INIT(Jp2File_Profile)(dec_jp2);
        if (!DEC(Jp2File_Profile)(dec_jp2, &bs, &err)) l.prof_reason = "decode";
        else if (!VALID(Jp2File_Profile)(dec_jp2, &err)) l.prof_reason = "constraint";
        else if ((r = cf_check_jp2_profile(dec_jp2)) != NULL) l.prof_reason = r;
        else l.prof_ok = 1;
    } else {
        INIT(JpxFile_Profile)(dec_jpx);
        if (!DEC(JpxFile_Profile)(dec_jpx, &bs, &err)) l.prof_reason = "decode";
        else if (!VALID(JpxFile_Profile)(dec_jpx, &err)) l.prof_reason = "constraint";
        else if ((r = cf_check_jpx_profile(dec_jpx)) != NULL) l.prof_reason = r;
        else l.prof_ok = 1;
    }
    return l;
}

static FILE *manifest;
static const char *out_dir;
static int emitted, emitted_valid;

/* Emit one vector: write the file, label it, append a manifest row. */
static void emit(Bytes b, cf_kind kind, const char *name, const char *field,
                 const char *note, const char *companions) {
    char file[256];
    Label l = label(b, kind);
    snprintf(file, sizeof file, "%s.%s", name, kind == CF_JP2 ? "jp2" : "jpx");
    write_file(out_dir, file, b);
    fprintf(manifest, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n", file,
            kind == CF_JP2 ? "jp2" : "jpx",
            l.std_ok ? "valid" : "invalid",
            l.prof_ok ? "valid" : "invalid",
            field ? field : "-",
            note ? note : (l.std_ok ? "-" : l.std_reason),
            companions ? companions : "-");
    emitted++;
    if (l.std_ok && l.prof_ok) emitted_valid++;
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
static Cod *cod_of(Jp2Family *f, int box) { return &cs_of(f, box)->segments.arr[0].body.u.cod.body; }
static TilePart *tp_of(Jp2Family *f, int box) { return &cs_of(f, box)->segments.arr[2].body.u.tilePart; }

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
SETTER(cod_progression, cod_of(f, box)->sgcod.progression)
SETTER(cod_layers,      cod_of(f, box)->sgcod.layers)
SETTER(cod_mct,         cod_of(f, box)->sgcod.mct)
SETTER(cod_levels,      cod_of(f, box)->spcod.levels)
SETTER(cod_cbw,         cod_of(f, box)->spcod.cbWidthExp)
SETTER(cod_cbh,         cod_of(f, box)->spcod.cbHeightExp)
SETTER(cod_cbstyle,     cod_of(f, box)->spcod.cbStyle)
SETTER(cod_transform,   cod_of(f, box)->spcod.transform)
SETTER(cod_ppx_lowest,  cod_of(f, box)->spcod.precincts.lowest.ppx)
SETTER(cod_ppx_higher,  cod_of(f, box)->spcod.precincts.higher.arr[0].ppx)
SETTER(cod_ppy_higher,  cod_of(f, box)->spcod.precincts.higher.arr[0].ppy)
SETTER(sot_lsot,   tp_of(f, box)->lsot)
SETTER(sot_isot,   tp_of(f, box)->isot)
SETTER(sot_tpsot,  tp_of(f, box)->tpsot)
SETTER(sot_tnsot,  tp_of(f, box)->tnsot)
SETTER(plt_zplt,   tp_of(f, box)->rest.headers.arr[0].body.u.plt.body.zplt)
SETTER(iplt_bits,  tp_of(f, box)->rest.headers.arr[0].body.u.plt.body.entries.arr[0].b0.bits)

typedef struct {
    const char *field;
    Setter set;
    asn1SccSint value;
    const char *note;
    int needs_precincts;
} FieldMutant;

static const FieldMutant field_mutants[] = {
    { "siz.rsiz",   set_siz_rsiz,   2,          "table A.10 value 2 (profile 1)", 0 },
    { "siz.rsiz",   set_siz_rsiz,   32768,      "amendment bit 15", 0 },
    { "siz.xsiz",   set_siz_xsiz,   0,          "min-1", 0 },
    { "siz.xsiz",   set_siz_xsiz,   2147483648, "profile max+1", 0 },
    { "siz.xsiz",   set_siz_xsiz,   4294967295u, "standard max (tile smaller: single-tile rule)", 0 },
    { "siz.ysiz",   set_siz_ysiz,   0,          "min-1", 0 },
    { "siz.ysiz",   set_siz_ysiz,   2147483648, "profile max+1", 0 },
    { "siz.xosiz",  set_siz_xosiz,  1,          "profile max+1; standard valid", 0 },
    { "siz.xosiz",  set_siz_xosiz,  4,          "== xsiz: empty image", 0 },
    { "siz.yosiz",  set_siz_yosiz,  1,          "profile max+1; standard valid", 0 },
    { "siz.xtsiz",  set_siz_xtsiz,  0,          "min-1", 0 },
    { "siz.xtsiz",  set_siz_xtsiz,  3,          "below xsiz: two tiles", 0 },
    { "siz.ytsiz",  set_siz_ytsiz,  3,          "below ysiz: two tiles", 0 },
    { "siz.xtosiz", set_siz_xtosiz, 1,          "tile origin past image origin", 0 },
    { "siz.ytosiz", set_siz_ytosiz, 1,          "tile origin past image origin", 0 },
    { "siz.csiz",   set_siz_csiz,   0,          "min-1", 0 },
    { "siz.csiz",   set_siz_csiz,   2,          "count mismatch", 0 },
    { "siz.csiz",   set_siz_csiz,   16385,      "max+1 (and count mismatch)", 0 },
    { "siz.component.depthMinus1", set_siz_depth, 38, "max+1 (39-bit depth)", 0 },
    { "siz.component.xrsiz", set_siz_xrsiz, 0, "min-1", 0 },
    { "siz.component.yrsiz", set_siz_yrsiz, 0, "min-1", 0 },
    { "cod.scod.reserved",   set_cod_reserved, 1, "reserved bit set", 0 },
    { "cod.sgcod.progression", set_cod_progression, 5, "max+1", 0 },
    { "cod.sgcod.progression", set_cod_progression, 3, "PCRL with unit sampling: valid", 0 },
    { "cod.sgcod.layers",    set_cod_layers, 0, "min-1", 0 },
    { "cod.sgcod.mct",       set_cod_mct, 2, "max+1", 0 },
    { "cod.spcod.levels",    set_cod_levels, 33, "max+1", 0 },
    { "cod.spcod.cbWidthExp", set_cod_cbw, 9, "max+1", 0 },
    { "cod.spcod.cbWidthExp", set_cod_cbw, 5, "5+4 = 9 > 8: code-block area", 0 },
    { "cod.spcod.cbHeightExp", set_cod_cbh, 9, "max+1", 0 },
    { "cod.spcod.cbStyle",   set_cod_cbstyle, 63, "all Part 1 bits: valid", 0 },
    { "cod.spcod.cbStyle",   set_cod_cbstyle, 64, "reserved bit 6 (HTJ2K flag)", 0 },
    { "cod.spcod.cbStyle",   set_cod_cbstyle, 255, "all bits", 0 },
    { "cod.spcod.transform", set_cod_transform, 2, "max+1", 0 },
    { "cod.spcod.precincts.lowest.ppx", set_cod_ppx_lowest, 0, "zero at r=0: valid", 1 },
    { "cod.spcod.precincts.higher[0].ppx", set_cod_ppx_higher, 0, "zero above r=0 (Table A.21)", 1 },
    { "cod.spcod.precincts.higher[0].ppy", set_cod_ppy_higher, 0, "zero above r=0 (Table A.21)", 1 },
    { "sot.lsot",   set_sot_lsot,  11,  "Lsot != 10", 0 },
    { "sot.isot",   set_sot_isot,  1,   "profile max+1 (tile 1): standard valid", 0 },
    { "sot.isot",   set_sot_isot,  65535, "standard max+1", 0 },
    { "sot.tpsot",  set_sot_tpsot, 1,   "first tile-part index 1: sequence", 0 },
    { "sot.tpsot",  set_sot_tpsot, 255, "reserved", 0 },
    { "sot.tnsot",  set_sot_tnsot, 0,   "unspecified count: valid", 0 },
    { "sot.tnsot",  set_sot_tnsot, 2,   "count 2 with one tile-part", 0 },
    { "plt.zplt",   set_plt_zplt,  1,   "index 1: valid", 0 },
    { "plt.iplt.b0.bits", set_iplt_bits, 0, "packet length 0: valid encoding", 0 },
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
    c->spcod.precincts.higher.nCount = c->spcod.levels + 1;    /* one byte too many */
}
static void rule_no_tile_part(Jp2Family *f, int box) {
    cs_of(f, box)->segments.nCount = 2;
}
static void rule_two_tile_parts(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    cs->segments.arr[3] = cs->segments.arr[2];
    cs->segments.arr[3].body.u.tilePart.tpsot = 1;
    cs->segments.arr[2].body.u.tilePart.tnsot = 2;
    cs->segments.arr[3].body.u.tilePart.tnsot = 2;
    cs->segments.nCount = 4;                              /* valid: two tile-parts */
}
static void rule_tnsot_inconsistent(Jp2Family *f, int box) {
    rule_two_tile_parts(f, box);
    cs_of(f, box)->segments.arr[3].body.u.tilePart.tnsot = 3;   /* first said 2 */
}
static void rule_tile_parts_65(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    int i;
    for (i = 1; i < 65; ++i) {
        cs->segments.arr[2 + i] = cs->segments.arr[2];
        cs->segments.arr[2 + i].body.u.tilePart.tpsot = i;
    }
    for (i = 0; i < 65; ++i) cs->segments.arr[2 + i].body.u.tilePart.tnsot = 65;
    cs->segments.nCount = 2 + 65;                         /* profile limit exceeded */
}
static void rule_com_segment(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    MainSegment *seg;
    cs->segments.arr[3] = cs->segments.arr[2];
    seg = &cs->segments.arr[2];
    seg->code = MC_COM;
    seg->body.kind = MainBody_com_PRESENT;
    seg->body.u.com.body.rcom = 1;
    OCTETS(seg->body.u.com.body.ccom, "esajpip corpus", 14);
    cs->segments.nCount = 4;                              /* valid */
}
static void rule_iplt_five_bytes(Jp2Family *f, int box) {
    Iplt *e = &tp_of(f, box)->rest.headers.arr[0].body.u.plt.body.entries.arr[0];
    e->b0.more = 1; e->b0.bits = 0; e->exist.b1 = 1;
    e->b1.more = 1; e->b1.bits = 0; e->exist.b2 = 1;
    e->b2.more = 1; e->b2.bits = 0; e->exist.b3 = 1;
    e->b3.more = 1; e->b3.bits = 0; e->exist.b4 = 1;
    e->b4.more = 0; e->b4.bits = 1;                        /* 5 bytes: profile max, valid */
}
static void rule_iplt_six_bytes(Jp2Family *f, int box) {
    Iplt *e = &tp_of(f, box)->rest.headers.arr[0].body.u.plt.body.entries.arr[0];
    rule_iplt_five_bytes(f, box);
    e->b4.more = 1; e->exist.b5 = 1;
    e->b5.more = 0; e->b5.bits = 1;                        /* 6 bytes: standard valid, profile invalid */
}
static void rule_iplt_too_short(Jp2Family *f, int box) {
    TilePart *tp = tp_of(f, box);
    tp->rest.data.nCount += 1;                              /* one byte no PLT entry covers */
    tp->rest.data.arr[tp->rest.data.nCount - 1] = 0x00;
}
static void rule_tile_cod_twice(Jp2Family *f, int box) {
    TilePart *tp = tp_of(f, box);
    int n = tp->rest.headers.nCount, i;
    for (i = 0; i < 2; ++i) {                               /* two COD in the tile header */
        TileSegment *ts = &tp->rest.headers.arr[n + i];
        ts->code = MC_COD;
        ts->body.kind = TileBody_cod_PRESENT;
        ts->body.u.cod.body = *cod_of(f, box);
    }
    tp->rest.headers.nCount = n + 2;
}
static void rule_tile_cod_second_part(Jp2Family *f, int box) {
    Codestream *cs = cs_of(f, box);
    TilePart *tp;
    TileSegment *ts;
    rule_two_tile_parts(f, box);
    tp = &cs->segments.arr[3].body.u.tilePart;             /* COD in tile-part 1 */
    ts = &tp->rest.headers.arr[tp->rest.headers.nCount++];
    ts->code = MC_COD;
    ts->body.kind = TileBody_cod_PRESENT;
    ts->body.u.cod.body = *cod_of(f, box);
}
static void rule_packet_count_overflow(Jp2Family *f, int box) {
    Siz *s = &cs_of(f, box)->siz.body;                     /* 2^16 x 2^16 default precincts */
    s->xsiz = s->xtsiz = 2147483647;
    s->ysiz = s->ytsiz = 2147483647;
}
static void rule_iplt_too_long(Jp2Family *f, int box) {
    Iplt *e = &tp_of(f, box)->rest.headers.arr[0].body.u.plt.body.entries.arr[0];
    e->b0.more = 0; e->b0.bits = 2;                        /* data holds 1 byte */
}
static void rule_pcrl_subsampled(Jp2Family *f, int box) {
    cod_of(f, box)->sgcod.progression = 3;
    cs_of(f, box)->siz.body.components.arr[0].xrsiz = 2;   /* profile: position order needs unit sampling */
}
static void rule_missing_signature(Jp2Family *f, int box) {
    int i;
    (void) box;
    for (i = 1; i < f->boxes.nCount; ++i) f->boxes.arr[i - 1] = f->boxes.arr[i];
    f->boxes.nCount--;                                    /* standard invalid; server accepts */
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
} RuleMutant;

static const RuleMutant rule_mutants[] = {
    { "codestream.one-cod-before-sot",  rule_second_cod,        "two COD in main header", 0 },
    { "codestream.one-qcd-before-sot",  rule_second_qcd,        "two QCD in main header", 0 },
    { "codestream.one-qcd-before-sot",  rule_no_qcd,            "no QCD", 0 },
    { "codestream.segment-after-sot",   rule_qcd_after_sot,     "QCD after the tile-part data", 0 },
    { "codestream.no-plt",              rule_no_plt,            "no PLT: standard valid, profile invalid", 0 },
    { "cod.precincts-count",            rule_precinct_count,    "levels+2 precinct bytes", 0 },
    { "codestream.no-tile-part",        rule_no_tile_part,      "main header only", 0 },
    { "sot.two-tile-parts",             rule_two_tile_parts,    "two tile-parts: valid", 0 },
    { "sot.tnsot-inconsistent",         rule_tnsot_inconsistent, "second SOT declares 3 tile-parts, first 2", 0 },
    { "codestream.tile-part-limit",     rule_tile_parts_65,     "65 tile-parts: profile invalid", 0 },
    { "main.com",                       rule_com_segment,       "COM segment: valid", 0 },
    { "plt.iplt-five-bytes",            rule_iplt_five_bytes,   "5-byte Iplt encoding value 1: valid", 0 },
    { "plt.sum-exceeds-data",           rule_iplt_too_long,     "packet length beyond tile-part data", 0 },
    { "plt.sum-short",                  rule_iplt_too_short,    "tile-part byte no PLT entry covers", 0 },
    { "tile.cod-once",                  rule_tile_cod_twice,    "two COD in the tile-part header", 0 },
    { "tile.cod-first-part",            rule_tile_cod_second_part, "COD in the second tile-part", 0 },
    { "codestream.packet-count",        rule_packet_count_overflow, "2^32 packets: profile invalid", 0 },
    { "plt.iplt-six-bytes",            rule_iplt_six_bytes,    "6-byte Iplt encoding value 1: valid", 0 },
    { "siz.position-order-sampling",    rule_pcrl_subsampled,   "PCRL with 2:1 sampling: profile invalid", 0 },
    { "file.signature",                 rule_missing_signature, "no jP box (T.800 I.4)", CF_JP2 },
    { "jp2.one-codestream",             rule_two_jp2c,          "two jp2c in .jp2", CF_JP2 },
};

/* Rule mutants specific to linked JPX (need the linked base). */
static void rule_dr_zero(Jp2Family *f, int box) { (void) box; f->boxes.arr[4].payload.u.ftbl.children.arr[0].payload.u.flst.fragments.arr[0].dr = 0; }
static void rule_dr_out_of_range(Jp2Family *f, int box) { (void) box; f->boxes.arr[4].payload.u.ftbl.children.arr[0].payload.u.flst.fragments.arr[0].dr = 3; }
static void rule_two_flst(Jp2Family *f, int box) {
    Superbox *sb = &f->boxes.arr[4].payload.u.ftbl;
    (void) box;
    sb->children.arr[1] = sb->children.arr[0];
    sb->children.nCount = 2;
}
static void rule_ndr_mismatch(Jp2Family *f, int box) { (void) box; f->boxes.arr[6].payload.u.dtbl.ndr = 3; }
static void rule_url_scheme(Jp2Family *f, int box) {
    (void) box;
    strcpy((char *) f->boxes.arr[6].payload.u.dtbl.references.arr[0].payload.u.url.loc, "http://x/frame1.jp2");
}
static void rule_url_jpx_target(Jp2Family *f, int box) {
    (void) box;
    strcpy((char *) f->boxes.arr[6].payload.u.dtbl.references.arr[0].payload.u.url.loc, "file://./frame1.jpx");
}
static void rule_url_version(Jp2Family *f, int box) { (void) box; f->boxes.arr[6].payload.u.dtbl.references.arr[0].payload.u.url.vers = 1; }
static void rule_mixed_linked_embedded(Jp2Family *f, int box) { (void) box; add_jp2c(f, BASE_W, BASE_H, 0, 0, 1); }
static void rule_no_dtbl(Jp2Family *f, int box) { (void) box; f->boxes.nCount = 6; }

static const RuleMutant linked_rule_mutants[] = {
    { "flst.dr-external",           rule_dr_zero,          "DR = 0 (this file): profile invalid", CF_JPX },
    { "flst.dr-range",              rule_dr_out_of_range,  "DR = ndr+1", CF_JPX },
    { "ftbl.one-flst",              rule_two_flst,         "two flst in one ftbl", CF_JPX },
    { "dtbl.ndr-count",             rule_ndr_mismatch,     "NDR = 3 with 2 url boxes", CF_JPX },
    { "url.file-scheme",            rule_url_scheme,       "http URL: profile invalid", CF_JPX },
    { "url.version",                rule_url_version,      "VERS = 1: profile invalid", CF_JPX },
    { "url.jp2-target",             rule_url_jpx_target,   "link to a .jpx: profile invalid", CF_JPX },
    { "jpx.linked-precedence",      rule_mixed_linked_embedded, "jp2c next to ftbl: links win, valid", CF_JPX },
    { "jpx.linked-shape",           rule_no_dtbl,          "no dtbl", CF_JPX },
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
    emit(m, kind, name, "jP.contents", "signature contents zeroed (T.800 I.5.1)", NULL);
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
            Bytes m = bytes_dup(valid);
            char name[256], field[64];
            if (r->len_width == 2) put16(m.data + r->len_off, (uint16_t) cand[k]);
            else put32(m.data + r->len_off, cand[k]);
            snprintf(field, sizeof field, "%s@0x%zx", r->kind, r->len_off);
            snprintf(name, sizeof name, "%s-len-%zx-%u", base_name, r->len_off, cand[k]);
            emit(m, kind, name, field, "length patched", NULL);
            free(m.data);
        }
    }
    free(rs.r);
}

/* Patch marker codes and box types in place: the body stays well-formed,
 * only its selector changes. Unknown codes are standard-invalid and
 * profile-valid (the server skips them); SIZ/SOC/SOT/EOC in the wrong place
 * are invalid at both layers. */
static void emit_code_mutants(Bytes valid, cf_kind kind, const char *base_name) {
    Regions rs = walk(&valid);
    int i;
    for (i = 0; i < rs.n; ++i) {
        const Region *r = &rs.r[i];
        static const struct { const char *kind; uint32_t value; const char *note; } patches[] = {
            { "Lxxx", 65392u,      "code FF70: undefined marker" },
            { "Lxxx", 65361u,      "code FF51: SIZ out of place" },
            { "Lxxx", 65424u,      "code FF90: SOT where a segment was" },
            { "LBox", 1633837924u, "type 'abcd': unknown box" },
            { "LBox", 1785737827u, "type 'jp2c' on a non-codestream box" },
        };
        size_t k;
        for (k = 0; k < sizeof patches / sizeof *patches; ++k) {
            Bytes m;
            char name[256], field[64];
            if (strcmp(r->kind, patches[k].kind) != 0) continue;
            m = bytes_dup(valid);
            if (r->len_width == 2) put16(m.data + r->start, (uint16_t) patches[k].value);
            else put32(m.data + r->start + 4, patches[k].value);
            snprintf(field, sizeof field, "%s@0x%zx", r->kind, r->start);
            snprintf(name, sizeof name, "%s-code-%zx-%u", base_name, r->start, patches[k].value);
            emit(m, kind, name, field, patches[k].note, NULL);
            free(m.data);
        }
    }
    free(rs.r);
}

/* Remove the last payload byte of one region and shrink it and every
 * enclosing length by one: lengths stay consistent, the innermost
 * `size deduced` list or fixed body is one byte short. */
static void emit_region_mutants(Bytes valid, cf_kind kind, const char *base_name) {
    Regions rs = walk(&valid);
    int i;
    {
        Bytes m = bytes_dup(valid);
        char name[256];
        m.len--;
        snprintf(name, sizeof name, "%s-short", base_name);
        emit(m, kind, name, "file", "last byte removed", NULL);
        free(m.data);
    }
    {
        Bytes m = { xcalloc(valid.len + 2), valid.len + 1 };
        char name[256];
        memcpy(m.data, valid.data, valid.len);
        snprintf(name, sizeof name, "%s-long", base_name);
        emit(m, kind, name, "file", "byte appended", NULL);
        free(m.data);
    }
    for (i = 0; i < rs.n; ++i) {
        const Region *r = &rs.r[i];
        size_t cut = r->end - 1;
        Bytes m;
        char name[256], field[64];
        int a;
        if (r->end <= r->payload_start) continue;
        m.len = valid.len - 1;
        m.data = xcalloc(m.len + 1);
        memcpy(m.data, valid.data, cut);
        memcpy(m.data + cut, valid.data + cut + 1, valid.len - cut - 1);
        for (a = i; a >= 0; a = rs.r[a].parent) {
            const Region *anc = &rs.r[a];
            if (anc->len_width == 2) put16(m.data + anc->len_off, (uint16_t) (be16(m.data + anc->len_off) - 1));
            else put32(m.data + anc->len_off, be32(m.data + anc->len_off) - 1);
        }
        snprintf(field, sizeof field, "%s@0x%zx", r->kind, r->start);
        snprintf(name, sizeof name, "%s-region-%zx", base_name, r->start);
        emit(m, kind, name, field, "region one byte short, lengths consistent", NULL);
        free(m.data);
    }
    free(rs.r);
}

/* ------------------------------------------------------------------------ */
/* Driver                                                                    */
/* ------------------------------------------------------------------------ */

static void run_base(Base base, const char *companions) {
    Bytes valid = encode_family(base.file, 1);
    char name[256];
    size_t i;
    Jp2Family *work = xcalloc(sizeof *work);

    emit(valid, base.kind, base.name, NULL, "base", companions);
    emit_signature_mutant(valid, base.kind, base.name);
    emit_length_mutants(valid, base.kind, base.name);
    emit_code_mutants(valid, base.kind, base.name);
    emit_region_mutants(valid, base.kind, base.name);

    if (base.jp2c_box >= 0) {
        int has_precincts = cod_of(base.file, base.jp2c_box)->scod.customPrecincts;
        for (i = 0; i < sizeof field_mutants / sizeof *field_mutants; ++i) {
            const FieldMutant *fm = &field_mutants[i];
            Bytes m;
            if (fm->needs_precincts != has_precincts) continue;
            *work = *base.file;
            fm->set(work, base.jp2c_box, fm->value);
            m = encode_family(work, 0);
            snprintf(name, sizeof name, "%s-%s-%lld", base.name, fm->field, (long long) fm->value);
            emit(m, base.kind, name, fm->field, fm->note, companions);
            free(m.data);
        }
        for (i = 0; i < sizeof rule_mutants / sizeof *rule_mutants; ++i) {
            const RuleMutant *rm = &rule_mutants[i];
            Bytes m;
            if (rm->only_kind && rm->only_kind != (int) base.kind) continue;
            if (has_precincts) continue;                   /* run structural mutants once */
            *work = *base.file;
            rm->apply(work, base.jp2c_box);
            m = encode_family(work, 0);
            snprintf(name, sizeof name, "%s-rule-%s-%zu", base.name, rm->name, i);
            emit(m, base.kind, name, rm->name, rm->note, companions);
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
            emit(m, base.kind, name, rm->name, rm->note, companions);
            free(m.data);
        }
    }
    free(work);
    free(valid.data);
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
    fprintf(manifest, "file\tkind\tstandard\tprofile\tfield\tnote\tcompanions\n");

    encode_buffer = xcalloc(FILE_BUFFER_BYTES);
    dec_family = xcalloc(sizeof *dec_family);
    dec_jp2 = xcalloc(sizeof *dec_jp2);
    dec_jpx = xcalloc(sizeof *dec_jpx);

    /* Sanity: the box-type constants in the model are the four-cc values. */
    if (be32((const unsigned char *) "jp2c") != BT_JP2C || be32((const unsigned char *) "flst") != BT_FLST)
        die("box type constants do not match four-cc values");

    run_base(base_jp2(0, 0), NULL);
    run_base(base_jp2(1, 1), NULL);
    run_base(base_jpx_embedded(), NULL);

    /* Linked JPX: emit the two companion frames first, read back their
     * codestream extents, then build the JPX that references them. The
     * server maps "file://./" to the image directory, so the test copies
     * the companions next to the vector. */
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
            free(rs.r);
            free(b.data);
            free(frame.file);
        }
        run_base(base_jpx_linked(urls, off, len, 2),
                 "jpx-linked-frame1.jp2,jpx-linked-frame2.jp2");
    }

    fclose(manifest);
    fprintf(stderr, "vectors: %d vectors written to %s (%d valid at both layers)\n",
            emitted, out_dir, emitted_valid);
    return EXIT_SUCCESS;
}
