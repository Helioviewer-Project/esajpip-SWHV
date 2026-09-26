/* test_transcode.c: tests of hv_transcode_file and hv_transcode_codestream,
 * run by CTest as "transcode" (label "tools").
 *
 * TRANSCODE_FIXTURES holds JP2 files in input/ and, under the same names
 * in kakadu/, what kdu_transcode made of them (FIXTURES.md). With the
 * environment variable TRANSCODE_ARCHIVE set to directories separated by
 * ':', every .jp2 file in them is transcoded too. */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_reader.h"
#include "hv_writer.h"
#include "transcode.h"

#ifndef TRANSCODE_FIXTURES
#error "TRANSCODE_FIXTURES must name the fixture directory"
#endif

static int failures, checks;

static void check(int ok, const char *format, ...) {
    va_list args;
    checks++;
    if (ok)
        return;
    failures++;
    fprintf(stderr, "FAIL: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* ------------------------------------------------------------------------
 * Byte buffers and files
 * ------------------------------------------------------------------------ */

typedef struct {
    uint8_t *data;
    size_t size;
} bytes;

static void bytes_free(bytes *b) {
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

static bytes bytes_copy(const uint8_t *data, size_t size) {
    bytes b;
    b.data = malloc(size ? size : 1);
    b.size = size;
    if (b.data == NULL)
        abort();
    memcpy(b.data, data, size);
    return b;
}

static void append(bytes *b, const void *data, size_t size) {
    uint8_t *grown = realloc(b->data, b->size + size + 1);
    if (grown == NULL)
        abort();
    b->data = grown;
    if (size)
        memcpy(b->data + b->size, data, size);
    b->size += size;
}

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, v >> 16);
    put16(p + 2, v & 0xFFFF);
}

static unsigned get16(const uint8_t *p) {
    return (unsigned)p[0] << 8 | p[1];
}

static bytes read_file(const char *path) {
    bytes b = {NULL, 0};
    FILE *f = fopen(path, "rb");
    uint8_t chunk[65536];
    size_t n;
    if (f == NULL) {
        check(0, "cannot open %s", path);
        return b;
    }
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
        append(&b, chunk, n);
    fclose(f);
    return b;
}

/* The payload of the first top-level box of `type`, or {NULL, 0}. */
static bytes box_payload(const bytes *file, uint32_t type) {
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at;
    bytes b = {NULL, 0};
    hv_boxes_file(&it, file->data, file->size);
    while (hv_boxes_next(&it, &box, &message, &at) == 1)
        if (box.type == type) {
            b.data = file->data + box.payload;
            b.size = box.end - box.payload;
            break;
        }
    return b;
}

/* A codestream without its main-header COM segments, which Kakadu writes
 * its own of. */
static bytes without_com(bytes cs) {
    bytes out = bytes_copy(cs.data, 2);
    size_t p = 2;
    while (p + 4 <= cs.size && get16(cs.data + p) != HV_SOT) {
        size_t n = 2 + get16(cs.data + p + 2);
        if (get16(cs.data + p) != HV_COM)
            append(&out, cs.data + p, n);
        p += n;
    }
    append(&out, cs.data + p, cs.size - p);
    return out;
}

/* ------------------------------------------------------------------------
 * Running the transcoder
 * ------------------------------------------------------------------------ */

/* NULL if the codestream in buf[start, end) reads through with these
 * hv_codestream_open flags, otherwise the reader's error. */
static const char *read_error(const uint8_t *buf, size_t start, size_t end, unsigned flags) {
    size_t at;
    return hv_codestream_check(buf, start, end, flags, &at);
}

/* A transcoded file is within the whole served profile. */
static void expect_served(const char *name, const uint8_t *buf, size_t size) {
    hv_box jp2c;
    size_t at;
    const char *error = hv_check_jp2(buf, size, &jp2c, &at);
    if (error == NULL)
        error = read_error(buf, jp2c.payload, jp2c.end, HV_PROFILE);
    check(error == NULL, "%s: output outside the served profile: %s", name, error);
}

typedef struct {
    int status;
    char error[256];
    bytes out;
} result;

/* Transcodes a codestream without the profile. An output whose main header
 * is within the served profile must be within all of it: the rest is what
 * the transcoder writes. */
static result transcode(const uint8_t *data, size_t size, int ppx, int ppy) {
    result r;
    hv_out out;
    hv_out_init(&out);
    r.status = hv_transcode_codestream(data, 0, size, ppx, ppy, 0, &out, r.error, sizeof r.error);
    r.out.data = out.data;
    r.out.size = out.size;
    if (r.status != 0 && out.size != 0)
        check(0, "a failed transcode left %zu bytes of output", out.size);
    if (r.status == 0 && read_error(out.data, 0, out.size, HV_PROFILE_HEADERS) == NULL) {
        const char *error = read_error(out.data, 0, out.size, HV_PROFILE);
        check(error == NULL, "an output outside the served profile: %s", error);
    }
    return r;
}

/* Accepted, with this output. */
static void expect_output(const char *name, const bytes *input, const bytes *expected) {
    result r = transcode(input->data, input->size, 7, 7);
    check(r.status == 0, "%s: rejected: %s", name, r.error);
    if (r.status == 0)
        check(r.out.size == expected->size && memcmp(r.out.data, expected->data, r.out.size) == 0,
              "%s: output differs", name);
    bytes_free(&r.out);
}

/* Rejected with exactly this message. */
static void expect_error(const char *name, const bytes *input, const char *message) {
    result r = transcode(input->data, input->size, 7, 7);
    check(r.status != 0, "%s: accepted, expected \"%s\"", name, message);
    if (r.status != 0)
        check(strcmp(r.error, message) == 0, "%s: \"%s\", expected \"%s\"", name, r.error, message);
    bytes_free(&r.out);
}

/* Transcoding an output again gives the same output. */
static void expect_stable(const char *name, const bytes *out) {
    result r = transcode(out->data, out->size, 7, 7);
    check(r.status == 0 && r.out.size == out->size && memcmp(r.out.data, out->data, out->size) == 0,
          "%s: output does not transcode to itself%s%s", name, r.status ? ": " : "",
          r.status ? r.error : "");
    bytes_free(&r.out);
}

/* ------------------------------------------------------------------------
 * The Kakadu references
 * ------------------------------------------------------------------------ */

/* The fixture outside the served profile: nonzero origins. */
#define ORIGIN_FIXTURE "synthetic_rgb_129x129_origin129_CPRL.jp2"

/* Every input transcodes, with -x, to its Kakadu reference: the same
 * boxes, the codestream equal except COM. The origin fixture's file is
 * rejected, and only its codestream is compared. */
static void test_references(void) {
    char path[4096];
    DIR *dir;
    struct dirent *e;
    int files = 0;

    snprintf(path, sizeof path, "%s/input", TRANSCODE_FIXTURES);
    if ((dir = opendir(path)) == NULL) {
        check(0, "cannot open %s", path);
        return;
    }
    while ((e = readdir(dir)) != NULL) {
        size_t n = strlen(e->d_name);
        bytes input, ref, got_cs, ref_cs, a, b;
        hv_out out;
        hv_boxes it_out, it_ref;
        hv_box bo, br;
        const char *message;
        char error[256];
        size_t at;
        int more_out, more_ref;
        if (n < 4 || strcmp(e->d_name + n - 4, ".jp2") != 0)
            continue;
        files++;
        snprintf(path, sizeof path, "%s/input/%s", TRANSCODE_FIXTURES, e->d_name);
        input = read_file(path);
        snprintf(path, sizeof path, "%s/kakadu/%s", TRANSCODE_FIXTURES, e->d_name);
        ref = read_file(path);
        hv_out_init(&out);
        if (strcmp(e->d_name, ORIGIN_FIXTURE) == 0) {
            bytes in_cs = box_payload(&input, HV_BOX_JP2C);
            result r;
            check(hv_transcode_file(input.data, input.size, 7, 7, 1, &out, error,
                                    sizeof error) != 0 &&
                  strcmp(error, "siz.zero-origin") == 0 && out.size == 0,
                  "%s: file accepted or rejected otherwise: %s", e->d_name, error);
            r = transcode(in_cs.data, in_cs.size, 7, 7);
            check(r.status == 0, "%s: codestream rejected: %s", e->d_name, r.error);
            if (r.status == 0) {
                a = without_com(r.out);
                b = without_com(box_payload(&ref, HV_BOX_JP2C));
                check(a.size == b.size && memcmp(a.data, b.data, a.size) == 0,
                      "%s: codestream differs from Kakadu's (COM aside)", e->d_name);
                bytes_free(&a);
                bytes_free(&b);
                expect_stable(e->d_name, &r.out);
            }
            bytes_free(&r.out);
            hv_out_free(&out);
            bytes_free(&input);
            bytes_free(&ref);
            continue;
        }
        if (hv_transcode_file(input.data, input.size, 7, 7, 1, &out, error, sizeof error) != 0) {
            check(0, "%s: rejected: %s", e->d_name, error);
            hv_out_free(&out);
            bytes_free(&input);
            bytes_free(&ref);
            continue;
        }
        expect_served(e->d_name, out.data, out.size);
        /* The same boxes; each equal, the codestream except COM. */
        hv_boxes_file(&it_out, out.data, out.size);
        hv_boxes_file(&it_ref, ref.data, ref.size);
        for (;;) {
            more_out = hv_boxes_next(&it_out, &bo, &message, &at) == 1;
            more_ref = hv_boxes_next(&it_ref, &br, &message, &at) == 1;
            if (!more_out || !more_ref)
                break;
            check(bo.type == br.type, "%s: box order differs from the reference", e->d_name);
            if (bo.type != br.type)
                break;
            if (bo.type != HV_BOX_JP2C) {
                check(bo.end - bo.payload == br.end - br.payload &&
                      memcmp(out.data + bo.payload, ref.data + br.payload, bo.end - bo.payload) == 0,
                      "%s: a box differs from the reference", e->d_name);
                continue;
            }
            got_cs.data = out.data + bo.payload;
            got_cs.size = bo.end - bo.payload;
            ref_cs.data = ref.data + br.payload;
            ref_cs.size = br.end - br.payload;
            a = without_com(got_cs);
            b = without_com(ref_cs);
            check(a.size == b.size && memcmp(a.data, b.data, a.size) == 0,
                  "%s: codestream differs from Kakadu's (COM aside)", e->d_name);
            bytes_free(&a);
            bytes_free(&b);
            expect_stable(e->d_name, &got_cs);
        }
        check(more_out == more_ref, "%s: box count differs from the reference", e->d_name);
        hv_out_free(&out);
        bytes_free(&input);
        bytes_free(&ref);
    }
    closedir(dir);
    check(files == 7, "expected 7 input fixtures, found %d", files);
}

/* ------------------------------------------------------------------------
 * Tile-parts and packets
 * ------------------------------------------------------------------------ */

static bytes fixture_codestream(const char *dir, const char *name, bytes *file) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s/%s", TRANSCODE_FIXTURES, dir, name);
    *file = read_file(path);
    return box_payload(file, HV_BOX_JP2C);
}

/* A codestream split at its first SOT: the main header and the tile's
 * data after SOD, the tile-part's PLT lengths, and the whole. */
typedef struct {
    bytes file, cs, main, body;
    uint64_t *lengths;
    size_t nlengths;
} split;

static void split_free(split *s) {
    bytes_free(&s->file);
    free(s->lengths);
    s->lengths = NULL;
}

static void split_codestream(split *s, const char *dir, const char *name) {
    size_t sot, p, end;
    uint64_t v = 0;
    memset(s, 0, sizeof *s);
    s->cs = fixture_codestream(dir, name, &s->file);
    for (sot = 2; get16(s->cs.data + sot) != HV_SOT; sot += 2 + get16(s->cs.data + sot + 2)) {}
    s->main.data = s->cs.data;
    s->main.size = sot;
    for (p = sot + 12; get16(s->cs.data + p) != HV_SOD; p += 2 + get16(s->cs.data + p + 2)) {
        size_t k;
        if (get16(s->cs.data + p) != HV_PLT)
            continue;
        end = p + 2 + get16(s->cs.data + p + 2);
        for (k = p + 5; k < end; k++) {
            v = v << 7 | (s->cs.data[k] & 0x7F);
            if (s->cs.data[k] < 0x80) {
                uint64_t *grown = realloc(s->lengths, (s->nlengths + 1) * sizeof *grown);
                if (grown == NULL)
                    abort();
                s->lengths = grown;
                s->lengths[s->nlengths++] = v;
                v = 0;
            }
        }
    }
    s->body.data = s->cs.data + p + 2;
    s->body.size = s->cs.size - 2 - (p + 2);
}

/* SOT, SOD and data of one tile-part; Psot = 0 when psot_zero. */
static void tile_part(bytes *b, unsigned isot, unsigned tpsot, unsigned tnsot,
                      const uint8_t *data, size_t size, int psot_zero) {
    uint8_t h[14];
    put16(h, HV_SOT);
    put16(h + 2, 10);
    put16(h + 4, isot);
    put32(h + 6, psot_zero ? 0 : (uint32_t)(14 + size));
    h[10] = (uint8_t)tpsot;
    h[11] = (uint8_t)tnsot;
    put16(h + 12, HV_SOD);
    append(b, h, 14);
    append(b, data, size);
}

static bytes codestream(const bytes *main) {
    return bytes_copy(main->data, main->size);
}

static void eoc(bytes *b) {
    static const uint8_t code[2] = {0xFF, 0xD9};
    append(b, code, 2);
}

/* The integrity cases: one tile split into tile-parts every way T.800
 * allows, and broken in the ways it does not. */
static void test_tile_parts(void) {
    split s;
    bytes expected, b;
    size_t first, total = 0, i;
    result r;

    split_codestream(&s, "kakadu", "2015_12_21__00_10_34_34__SDO_AIA_AIA_171.jp2");
    for (i = 0; i < s.nlengths; i++)
        total += s.lengths[i];
    check(s.nlengths > 0 && total == s.body.size, "AIA reference: PLT does not cover its tile");
    first = (size_t)s.lengths[0];
    r = transcode(s.cs.data, s.cs.size, 7, 7);
    check(r.status == 0, "AIA reference: rejected: %s", r.error);
    expected = r.out;

    b = codestream(&s.main);                    /* empty parts around two */
    tile_part(&b, 0, 0, 4, NULL, 0, 0);
    tile_part(&b, 0, 1, 4, s.body.data, first, 0);
    tile_part(&b, 0, 2, 4, s.body.data + first, s.body.size - first, 0);
    tile_part(&b, 0, 3, 4, NULL, 0, 0);
    eoc(&b);
    expect_output("four tile-parts, two empty", &b, &expected);
    bytes_free(&b);

    b = codestream(&s.main);                    /* the last part with Psot = 0 */
    tile_part(&b, 0, 0, 2, s.body.data, first, 0);
    tile_part(&b, 0, 1, 2, s.body.data + first, s.body.size - first, 1);
    eoc(&b);
    expect_output("Psot = 0 on the last tile-part", &b, &expected);
    b.size -= 2;
    expect_error("Psot = 0 without EOC", &b, "Psot = 0 but the codestream does not end with EOC");
    bytes_free(&b);

    b = codestream(&s.main);
    tile_part(&b, 0, 0, 0, s.body.data, s.body.size, 0);
    eoc(&b);
    expect_output("TNsot = 0", &b, &expected);
    bytes_free(&b);

    b = codestream(&s.main);
    tile_part(&b, 0, 0, 2, s.body.data, first - 1, 0);
    tile_part(&b, 0, 1, 2, s.body.data + first - 1, s.body.size - first + 1, 0);
    eoc(&b);
    expect_error("a packet across two tile-parts", &b, "packet crosses a tile-part boundary");
    bytes_free(&b);

    b = codestream(&s.main);
    tile_part(&b, 0, 0, 1, s.body.data, s.body.size, 0);
    append(&b, "", 1);                          /* inside the tile-part */
    put32(b.data + s.main.size + 6, (uint32_t)(14 + s.body.size + 1));
    eoc(&b);
    expect_error("a byte after the last packet", &b, "1 unparsed tile bytes");
    bytes_free(&b);

    b = bytes_copy(s.cs.data, s.cs.size);
    append(&b, "", 1);
    expect_error("a byte after EOC", &b, "bytes after EOC");
    b.size = s.cs.size - 2;
    expect_error("no EOC", &b, "tile-part overruns the codestream");
    bytes_free(&b);

    b = codestream(&s.main);
    tile_part(&b, 1, 0, 1, s.body.data, s.body.size, 0);
    eoc(&b);
    expect_error("tile 1 of 1", &b, "sot.isot-range");
    bytes_free(&b);

    b = codestream(&s.main);
    tile_part(&b, 0, 1, 1, s.body.data, s.body.size, 0);
    eoc(&b);
    expect_error("tile-part 1 first", &b, "sot.tpsot-sequence");
    bytes_free(&b);

    b = codestream(&s.main);
    tile_part(&b, 0, 0, 2, s.body.data, s.body.size, 0);
    eoc(&b);
    expect_error("one of two tile-parts", &b, "sot.tnsot-count");
    bytes_free(&b);

    b = codestream(&s.main);
    tile_part(&b, 0, 0, 2, s.body.data, first, 0);
    tile_part(&b, 0, 1, 3, s.body.data + first, s.body.size - first, 0);
    eoc(&b);
    expect_error("TNsot 2 then 3", &b, "sot.tnsot-inconsistent");
    bytes_free(&b);

    b = codestream(&s.main);
    tile_part(&b, 0, 0, 1, s.body.data, s.body.size - (size_t)s.lengths[s.nlengths - 1], 0);
    eoc(&b);
    expect_error("without the last packet", &b, "truncated input is not supported: missing packets");
    bytes_free(&b);

    b = codestream(&s.main);
    tile_part(&b, 0, 0, 1, s.body.data, s.body.size - 1, 0);
    eoc(&b);
    expect_error("without the last byte", &b, "truncated input is not supported: missing packets");
    bytes_free(&b);

    b = codestream(&s.main);                    /* Psot = 0 before the last */
    tile_part(&b, 0, 0, 0, s.body.data, first, 1);
    tile_part(&b, 0, 1, 0, s.body.data + first, s.body.size - first, 0);
    eoc(&b);
    expect_error("Psot = 0 on the first of two", &b, "Psot=0 is only valid for the last tile-part");
    bytes_free(&b);

    b = codestream(&s.main);
    tile_part(&b, 0, 0, 2, s.body.data, first, 1);
    tile_part(&b, 0, 1, 2, s.body.data + first, s.body.size - first, 0);
    eoc(&b);
    expect_error("Psot = 0 on the first of two, TNsot = 2", &b,
                 "sot.tnsot-count");
    bytes_free(&b);

    bytes_free(&expected);
    split_free(&s);

    /* A run of 1 bits (0xFF 0x7F pairs, legal stuffing) grows Lblock past
     * 62 bits: the length saturates instead of wrapping around. */
    split_codestream(&s, "input", "synthetic_rgb_129x129_origin129_CPRL.jp2");
    b = codestream(&s.main);
    {
        bytes tile = bytes_copy(s.body.data, 2917);
        static const uint8_t ones[2] = {0xFF, 0x7F};
        for (i = 0; i < 20; i++)
            append(&tile, ones, 2);
        append(&tile, s.body.data + 2932, s.body.size - 2932);
        tile_part(&b, 0, 0, 1, tile.data, tile.size, 0);
        bytes_free(&tile);
    }
    eoc(&b);
    expect_error("Lblock past 62 bits", &b,
                 "truncated input is not supported: packet data overruns the tile");
    bytes_free(&b);
    split_free(&s);
}

/* ------------------------------------------------------------------------
 * Main headers
 * ------------------------------------------------------------------------ */

/* rgb with `size` bytes at `offset` replaced by `value`. */
static bytes patched(const bytes *rgb, size_t offset, const void *value, size_t size) {
    bytes b = bytes_copy(rgb->data, rgb->size);
    memcpy(b.data + offset, value, size);
    return b;
}

static bytes concat3(const uint8_t *a, size_t na, const uint8_t *b, size_t nb,
                     const uint8_t *c, size_t nc) {
    bytes out = bytes_copy(a, na);
    append(&out, b, nb);
    append(&out, c, nc);
    return out;
}

#define EXPECT_PATCHED(name, offset, value, message)                        \
    do {                                                                     \
        static const uint8_t v_[] = value;                                   \
        bytes b_ = patched(&rgb, (offset), v_, sizeof v_);                    \
        expect_error((name), &b_, (message));                                \
        bytes_free(&b_);                                                     \
    } while (0)
#define BYTES(...) {__VA_ARGS__}

static void test_headers(void) {
    bytes file, cs = fixture_codestream("input", "synthetic_rgb_129x129_origin129_CPRL.jp2", &file);
    bytes rgb = bytes_copy(cs.data, cs.size), b;
    size_t siz = 2, cod = siz + 2 + get16(rgb.data + siz + 2), sot = cod, n;
    uint8_t *p;

    while (get16(rgb.data + sot) != HV_SOT)
        sot += 2 + get16(rgb.data + sot + 2);

    b.data = rgb.data;
    b.size = 0;
    expect_error("empty", &b, "codestream does not start with SOC");
    b.size = 4;
    expect_error("SOC and SIZ code only", &b, "truncated SIZ");
    b = concat3(rgb.data, 2, rgb.data + cod, rgb.size - cod, NULL, 0);
    expect_error("no SIZ", &b, "SOC is not followed by SIZ");
    bytes_free(&b);
    b = concat3(rgb.data, cod, rgb.data + sot, rgb.size - sot, NULL, 0);
    expect_error("no COD or QCD", &b, "codestream.one-cod-before-sot");
    bytes_free(&b);
    b = concat3(rgb.data, cod, rgb.data + siz, cod - siz, rgb.data + cod, rgb.size - cod);
    expect_error("two SIZ", &b, "main.marker-code");
    bytes_free(&b);
    b = concat3(rgb.data, sot, rgb.data + cod, sot - cod, rgb.data + sot, rgb.size - sot);
    expect_error("two COD", &b, "codestream.one-cod-before-sot");
    bytes_free(&b);

    EXPECT_PATCHED("Lsiz 2", siz + 2, BYTES(0x00, 0x02), "invalid SIZ");
    EXPECT_PATCHED("Lsiz 65535", siz + 2, BYTES(0xFF, 0xFF), "truncated SIZ");
    EXPECT_PATCHED("Csiz 0", siz + 4 + 34, BYTES(0x00, 0x00), "invalid SIZ");
    EXPECT_PATCHED("Csiz 4 of 3", siz + 4 + 34, BYTES(0x00, 0x04),
                   "siz.csiz-count");
    EXPECT_PATCHED("XOsiz past Xsiz", siz + 4 + 10, BYTES(0x00, 0x00, 0x01, 0x02),
                   "siz.origin-inside");
    EXPECT_PATCHED("XTsiz 0", siz + 4 + 18, BYTES(0x00, 0x00, 0x00, 0x00), "invalid SIZ");
    EXPECT_PATCHED("XRsiz 0", siz + 4 + 37, BYTES(0x00), "invalid SIZ");

    /* Components subsampled so much that coarse resolutions are empty. */
    b = bytes_copy(rgb.data, rgb.size);
    b.data[siz + 4 + 40] = 174;
    b.data[siz + 4 + 44] = 255;
    expect_error("empty resolutions", &b, "5569 unparsed tile bytes");
    bytes_free(&b);

    EXPECT_PATCHED("Lcod 0", cod + 2, BYTES(0x00, 0x00), "marker segment overruns its header");
    /* One precinct byte short. */
    n = get16(rgb.data + cod + 2);
    b = concat3(rgb.data, cod + n + 1, rgb.data + cod + n + 2, rgb.size - cod - n - 2, NULL, 0);
    put16(b.data + cod + 2, (unsigned)n - 1);
    expect_error("short COD", &b, "cod.precincts-count");
    bytes_free(&b);
    EXPECT_PATCHED("Scod reserved bit", cod + 4, BYTES(0x08), "invalid COD");
    EXPECT_PATCHED("progression 5", cod + 4 + 1, BYTES(0x05), "invalid COD");
    EXPECT_PATCHED("0 layers", cod + 4 + 2, BYTES(0x00, 0x00), "invalid COD");
    EXPECT_PATCHED("66 levels", cod + 4 + 5, BYTES(0x42), "invalid COD");
    EXPECT_PATCHED("xcb 11", cod + 4 + 6, BYTES(0x09), "invalid COD");
    EXPECT_PATCHED("xcb + ycb 13", cod + 4 + 7, BYTES(0x05), "cod.codeblock-area");
    EXPECT_PATCHED("PPy 0 at resolution 1", cod + 4 + 11, BYTES(0x08),
                   "cod.precincts-higher-zero");

    /* A huge image and many layers with little data. */
    b = bytes_copy(rgb.data, rgb.size);
    put32(b.data + siz + 4 + 2, 0x7FFFFFFF);
    put32(b.data + siz + 4 + 18, 0x7FFFFFFF);
    put16(b.data + cod + 4 + 2, 60164);
    expect_error("more packets than bytes", &b, "packet count exceeds tile data");
    bytes_free(&b);
    /* Large sparse image, largest precincts: too many code-blocks. */
    b = bytes_copy(rgb.data, rgb.size);
    p = b.data + siz + 4;
    put32(p + 2, 65536);                        /* Xsiz, Ysiz */
    put32(p + 6, 65536);
    put32(p + 18, 65536);                       /* XTsiz, YTsiz */
    put32(p + 22, 65536);
    memset(b.data + cod + 4 + 10, 0xFF, 3);     /* 2^15 precincts */
    expect_error("too many code-blocks", &b, "code-block count exceeds supported limit");
    put16(b.data + cod + 4 + 2, 200);
    expect_error("too many code-blocks, 200 layers", &b, "code-block count exceeds supported limit");
    bytes_free(&b);

    bytes_free(&rgb);
    bytes_free(&file);
}

/* ------------------------------------------------------------------------
 * The served profile: what hv_transcode_file accepts
 * ------------------------------------------------------------------------ */

static uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) << 16 | get16(p + 2); }

/* A minimal JP2 file around a codestream: signature, file type with this
 * brand and compatibility entry, a JP2 Header box as SIZ describes the
 * image (ihdr; colr greyscale or sRGB; its components share one depth),
 * codestream box. */
static bytes jp2_file(const bytes *cs, const char *brand) {
    uint8_t h[32] = {0, 0, 0, 12, 'j', 'P', ' ', ' ', 0x0D, 0x0A, 0x87, 0x0A,
                     0, 0, 0, 20, 'f', 't', 'y', 'p'};
    uint8_t jp2h[45] = {0, 0, 0, 45, 'j', 'p', '2', 'h', 0, 0, 0, 22, 'i', 'h', 'd', 'r'};
    const uint8_t *siz = cs->data + 4;          /* Lsiz */
    unsigned nc = get16(siz + 36);
    bytes b = {NULL, 0};
    memcpy(h + 20, brand, 4);
    memset(h + 24, 0, 4);
    memcpy(h + 28, brand, 4);
    put32(jp2h + 16, get32(siz + 8) - get32(siz + 16));     /* Ysiz - YOsiz */
    put32(jp2h + 20, get32(siz + 4) - get32(siz + 12));     /* Xsiz - XOsiz */
    put16(jp2h + 24, nc);
    jp2h[26] = siz[38];                                     /* Ssiz of component 0 */
    jp2h[27] = 7;
    memcpy(jp2h + 30, "\0\0\0\x0F" "colr" "\x01\0\0", 11);   /* METH 1, PREC, APPROX */
    put32(jp2h + 41, nc == 1 ? 17 : 16);                    /* greyscale or sRGB */
    append(&b, h, sizeof h);
    append(&b, jp2h, sizeof jp2h);
    put32(h, (uint32_t)(8 + cs->size));
    memcpy(h + 4, "jp2c", 4);
    append(&b, h, 8);
    append(&b, cs->data, cs->size);
    return b;
}

/* The file is rejected, with an error that starts with `message`. */
static void expect_file_error(const char *name, const bytes *file, const char *message) {
    hv_out out;
    char error[256];
    hv_out_init(&out);
    check(hv_transcode_file(file->data, file->size, 7, 7, 0, &out, error, sizeof error) != 0 &&
          strncmp(error, message, strlen(message)) == 0 && out.size == 0,
          "%s: \"%s\", expected \"%s\"", name, out.size ? "accepted" : error, message);
    hv_out_free(&out);
}

/* The file is accepted, the output is served and its codestream is the
 * codestream-level transcode. */
static void expect_file_output(const char *name, const bytes *file, const bytes *cs) {
    hv_out out;
    char error[256];
    result r = transcode(cs->data, cs->size, 7, 7);
    hv_out_init(&out);
    if (hv_transcode_file(file->data, file->size, 7, 7, 0, &out, error, sizeof error) != 0) {
        check(0, "%s: rejected: %s", name, error);
    } else {
        bytes got = {out.data, out.size}, got_cs = box_payload(&got, HV_BOX_JP2C);
        expect_served(name, out.data, out.size);
        check(r.status == 0 && got_cs.size == r.out.size &&
              memcmp(got_cs.data, r.out.data, got_cs.size) == 0,
              "%s: codestream differs from the codestream-level transcode", name);
    }
    hv_out_free(&out);
    bytes_free(&r.out);
}

static void test_served_profile(void) {
    static const uint8_t rgn[] = {0xFF, 0x5E, 0x00, 0x05, 0x00, 0x00, 0x07};
    static const struct {
        const char *name;
        uint8_t code;
        const char *codestream_error;
    } layout[] = {
        {"COC", 0x53, "unsupported main header marker 0xFF53"},
        {"POC", 0x5F, "unsupported main header marker 0xFF5F"},
        {"PPM", 0x60, "unsupported main header marker 0xFF60"},
    };
    bytes file, cs = fixture_codestream("input", "solo_fsi174_127x129_RLCP_PLT.jp2", &file);
    bytes b, f;
    size_t sot = 2, i;
    result plain, with_rgn;

    while (get16(cs.data + sot) != HV_SOT)
        sot += 2 + get16(cs.data + sot + 2);

    /* The fixture, wrapped minimally: accepted. */
    f = jp2_file(&cs, "jp2 ");
    expect_file_output("minimal JP2", &f, &cs);
    bytes_free(&f);

    /* Not JP2 files. */
    expect_file_error("raw codestream", &cs, "file.signature at 0");
    f = jp2_file(&cs, "jpx ");
    expect_file_error("JPX brand", &f, "file.ftyp-brand at 12");
    bytes_free(&f);
    {
        bytes box = box_payload(&file, HV_BOX_JP2C);           /* 8-byte box header */
        b = bytes_copy(file.data, file.size);
        append(&b, box.data - 8, box.size + 8);
        expect_file_error("two codestreams", &b, "jp2.one-codestream");
        bytes_free(&b);
    }

    /* COC, POC and PPM: rejected by the transcoder, and outside the
     * served profile's main header. */
    for (i = 0; i < sizeof layout / sizeof *layout; i++) {
        uint8_t segment[5] = {0xFF, layout[i].code, 0x00, 0x03, 0x00};
        b = concat3(cs.data, sot, segment, sizeof segment, cs.data + sot, cs.size - sot);
        expect_error(layout[i].name, &b, layout[i].codestream_error);
        f = jp2_file(&b, "jp2 ");
        expect_file_error(layout[i].name, &f, "main.marker-code");
        bytes_free(&f);
        bytes_free(&b);
    }

    /* RGN is kept where it was, the output otherwise unchanged. */
    b = concat3(cs.data, sot, rgn, sizeof rgn, cs.data + sot, cs.size - sot);
    plain = transcode(cs.data, cs.size, 7, 7);
    with_rgn = transcode(b.data, b.size, 7, 7);
    check(plain.status == 0 && with_rgn.status == 0, "RGN: rejected: %s", with_rgn.error);
    if (plain.status == 0 && with_rgn.status == 0) {
        size_t out_sot = 2;
        bytes expected;
        while (get16(plain.out.data + out_sot) != HV_SOT)
            out_sot += 2 + get16(plain.out.data + out_sot + 2);
        expected = concat3(plain.out.data, out_sot, rgn, sizeof rgn, plain.out.data + out_sot,
                           plain.out.size - out_sot);
        check(with_rgn.out.size == expected.size &&
              memcmp(with_rgn.out.data, expected.data, expected.size) == 0,
              "RGN: output is not the plain one with RGN");
        bytes_free(&expected);
    }
    f = jp2_file(&b, "jp2 ");
    expect_file_output("RGN", &f, &b);
    bytes_free(&f);
    bytes_free(&plain.out);
    bytes_free(&with_rgn.out);
    bytes_free(&b);
    bytes_free(&file);
}

/* The boxes around the codestream: copied as read and in order, LBox = 0
 * included; superboxes' children checked; the XML box with -x. */
static void test_boxes(void) {
    /* A last superbox with LBox = 0 whose last child has LBox = 0 too. */
    static const uint8_t to_end[] = {0, 0, 0, 0, 'a', 's', 'o', 'c',
                                     0, 0, 0, 0, 'l', 'b', 'l', ' ', 'x'};
    static const uint8_t bad_child[] = {0, 0, 0, 17, 'u', 'i', 'n', 'f',
                                        0, 0, 0, 7, 'u', 'l', 's', 't', 'x'};
    static const uint8_t bad_xml[] = {0, 0, 0, 11, 'x', 'm', 'l', ' ', '<', 'a', '>'};
    bytes file, cs = fixture_codestream("input", "solo_fsi174_127x129_RLCP_PLT.jp2", &file);
    bytes f = jp2_file(&cs, "jp2 "), g;
    hv_out once, twice;
    char error[256];

    g = bytes_copy(f.data, f.size);
    append(&g, to_end, sizeof to_end);
    hv_out_init(&once);
    hv_out_init(&twice);
    if (hv_transcode_file(g.data, g.size, 7, 7, 0, &once, error, sizeof error) != 0) {
        check(0, "LBox = 0 superbox: rejected: %s", error);
    } else {
        expect_served("LBox = 0 superbox", once.data, once.size);
        check(once.size >= sizeof to_end &&
              memcmp(once.data + once.size - sizeof to_end, to_end, sizeof to_end) == 0,
              "LBox = 0 superbox: not copied as read");
        error[0] = 0;
        check(hv_transcode_file(once.data, once.size, 7, 7, 0, &twice, error, sizeof error) == 0 &&
              twice.size == once.size && memcmp(twice.data, once.data, once.size) == 0,
              "LBox = 0 superbox: output does not transcode to itself: %s", error);
    }
    hv_out_free(&once);
    hv_out_free(&twice);
    bytes_free(&g);

    g = bytes_copy(f.data, f.size);
    append(&g, bad_child, sizeof bad_child);
    expect_file_error("uinf child with LBox 7", &g, "invalid or truncated box header at ");
    bytes_free(&g);

    g = bytes_copy(f.data, f.size);
    append(&g, bad_xml, sizeof bad_xml);
    hv_out_init(&once);
    check(hv_transcode_file(g.data, g.size, 7, 7, 1, &once, error, sizeof error) != 0 &&
          strncmp(error, "XML box at ", 11) == 0 && once.size == 0,
          "-x, unterminated XML: \"%s\"", once.size ? "accepted" : error);
    hv_out_free(&once);
    hv_out_init(&once);
    check(hv_transcode_file(g.data, g.size, 7, 7, 0, &once, error, sizeof error) == 0,
          "unterminated XML without -x: rejected: %s", error);
    hv_out_free(&once);
    bytes_free(&g);

    /* Superboxes nested HV_BOX_DEPTH_MAX deep are copied; one more level
     * is refused instead of recursing further. */
    {
        int depth;
        for (depth = HV_BOX_DEPTH_MAX; depth <= HV_BOX_DEPTH_MAX + 1; depth++) {
            int k;
            g = bytes_copy(f.data, f.size);
            for (k = 0; k < depth; k++) {
                uint8_t h[8] = {0, 0, 0, 0, 'a', 's', 'o', 'c'};
                put32(h, (uint32_t)(8 * (depth - k)));
                append(&g, h, sizeof h);
            }
            hv_out_init(&once);
            error[0] = 0;
            if (depth == HV_BOX_DEPTH_MAX)
                check(hv_transcode_file(g.data, g.size, 7, 7, 0, &once, error, sizeof error) == 0,
                      "%d nested superboxes: rejected: %s", depth, error);
            else
                check(hv_transcode_file(g.data, g.size, 7, 7, 0, &once, error, sizeof error) != 0 &&
                          strncmp(error, "boxes nested deeper than", 24) == 0,
                      "%d nested superboxes: \"%s\"", depth, error[0] ? error : "accepted");
            hv_out_free(&once);
            bytes_free(&g);
        }
    }

    bytes_free(&f);
    bytes_free(&file);
}

/* ------------------------------------------------------------------------
 * Corrupted tile data: every result is a rejection or a stable output
 * ------------------------------------------------------------------------ */

static uint32_t rng_state = 1;

static uint32_t rng(void) {                     /* xorshift32 */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static size_t below(size_t n) {
    return n ? rng() % n : 0;
}

static void test_corruption(void) {
    static const char *const names[3] = {
        "solo_fsi174_127x129_RLCP_PLT.jp2", "synthetic_rgb_129x129_origin129_CPRL.jp2",
        "synthetic_rgb_129x129_CPRL_SOP_EPH.jp2"};
    static const uint8_t runs[4] = {0xFF, 0x00, 0x7F, 0x80};
    split s[3];
    int i, k, accepted = 0, rejected = 0;

    for (i = 0; i < 3; i++)
        split_codestream(&s[i], "input", names[i]);
    for (i = 0; i < 2000; i++) {
        split *o = &s[below(3)];
        bytes tile = bytes_copy(o->body.data, o->body.size), b;
        size_t at = below(tile.size - 16), n, m;
        result r;
        switch (below(4)) {
        case 0:                                 /* a few bytes changed */
            for (k = 1 + (int)below(8); k > 0; k--)
                tile.data[below(tile.size)] = (uint8_t)rng();
            break;
        case 1:                                 /* cut short */
            tile.size = at;
            break;
        case 2: {                               /* n bytes replaced by a run of m */
            uint8_t v = runs[below(4)];
            n = 1 + below(16);
            m = 1 + below(16);
            b = bytes_copy(tile.data, at);
            for (k = 0; k < (int)m; k++)
                append(&b, &v, 1);
            if (at + n < tile.size)
                append(&b, tile.data + at + n, tile.size - at - n);
            bytes_free(&tile);
            tile = b;
            break;
        }
        default:                                /* random bytes inserted */
            b = bytes_copy(tile.data, at);
            for (k = 1 + (int)below(32); k > 0; k--) {
                uint8_t v = (uint8_t)rng();
                append(&b, &v, 1);
            }
            append(&b, tile.data + at, tile.size - at);
            bytes_free(&tile);
            tile = b;
        }
        b = codestream(&o->main);
        tile_part(&b, 0, 0, 1, tile.data, tile.size, 0);
        eoc(&b);
        r = transcode(b.data, b.size, 7, 7);
        if (r.status != 0) {
            rejected++;
        } else {
            accepted++;
            expect_stable("corrupted tile", &r.out);
        }
        bytes_free(&r.out);
        bytes_free(&b);
        bytes_free(&tile);
    }
    check(accepted > 0 && rejected > 0, "corruption: %d accepted, %d rejected", accepted, rejected);
    for (i = 0; i < 3; i++)
        split_free(&s[i]);
}

/* ------------------------------------------------------------------------
 * SOP, EPH and bit stuffing (A.8.1, A.8.2, B.10.1)
 * ------------------------------------------------------------------------ */

static size_t find_all(const bytes *b, const uint8_t *pattern, size_t n, size_t *at, size_t max) {
    size_t i, count = 0;
    for (i = 0; i + n <= b->size && count < max; i++)
        if (memcmp(b->data + i, pattern, n) == 0)
            at[count++] = i;
    return count;
}

static void set_psot(bytes *b) {
    size_t sot = 2;
    while (get16(b->data + sot) != HV_SOT)
        sot += 2 + get16(b->data + sot + 2);
    put32(b->data + sot + 6, (uint32_t)(b->size - 2 - sot));
}

static void test_packet_rules(void) {
    static const uint8_t sop_code[4] = {0xFF, 0x91, 0x00, 0x04}, eph_code[2] = {0xFF, 0x92};
    bytes file, cs = fixture_codestream("input", "synthetic_rgb_129x129_CPRL_SOP_EPH.jp2", &file);
    bytes b, noplt;
    size_t sop[64], eph[64], nsop, neph, i, sot, p;
    char message[128];

    nsop = find_all(&cs, sop_code, 4, sop, 64);
    neph = find_all(&cs, eph_code, 2, eph, 64);
    check(nsop == 27 && neph == 27, "SOP/EPH fixture: %zu SOP, %zu EPH", nsop, neph);
    if (nsop < 8 || neph < 8)
        return;

    b = bytes_copy(cs.data, cs.size);
    b.data[sop[5] + 5] ^= 1;
    expect_error("Nsop out of sequence", &b, "invalid SOP marker segment before packet 5");
    bytes_free(&b);
    b = bytes_copy(cs.data, cs.size);
    b.data[sop[5] + 3] = 5;
    expect_error("Lsop 5", &b, "invalid SOP marker segment before packet 5");
    bytes_free(&b);

    /* A header byte after 0xFF with its top bit set. */
    for (i = 0; i < nsop; i++) {
        size_t k;
        for (k = sop[i] + 6; k + 1 < eph[i]; k++)
            if (cs.data[k] == 0xFF)
                break;
        if (k + 1 < eph[i]) {
            b = bytes_copy(cs.data, cs.size);
            b.data[k + 1] |= 0x80;
            snprintf(message, sizeof message, "invalid bit stuffing in packet header %zu", i);
            expect_error("bad stuffing", &b, message);
            bytes_free(&b);
            break;
        }
    }
    check(i < nsop, "SOP/EPH fixture: no packet header with 0xFF");

    /* EPH missing after one header: without the PLT, which would no longer
     * add up. */
    noplt = bytes_copy(cs.data, cs.size);
    for (sot = 2; get16(noplt.data + sot) != HV_SOT; sot += 2 + get16(noplt.data + sot + 2)) {}
    for (p = sot + 12; get16(noplt.data + p) != HV_SOD;) {
        size_t n = 2 + get16(noplt.data + p + 2);
        if (get16(noplt.data + p) == HV_PLT) {
            memmove(noplt.data + p, noplt.data + p + n, noplt.size - p - n);
            noplt.size -= n;
        } else {
            p += n;
        }
    }
    set_psot(&noplt);
    {
        result r = transcode(noplt.data, noplt.size, 7, 7);
        check(r.status == 0, "SOP/EPH fixture without PLT: rejected: %s", r.error);
        bytes_free(&r.out);
    }
    neph = find_all(&noplt, eph_code, 2, eph, 64);
    b = bytes_copy(noplt.data, noplt.size);
    memmove(b.data + eph[7], b.data + eph[7] + 2, b.size - eph[7] - 2);
    b.size -= 2;
    set_psot(&b);
    expect_error("EPH missing", &b, "missing EPH marker after packet header 7");
    bytes_free(&b);
    bytes_free(&noplt);
    bytes_free(&file);
}

/* ------------------------------------------------------------------------
 * Memory bounds for what headers declare
 * ------------------------------------------------------------------------ */

/* A codestream with ncomps components (sub-sampling xr, grid origin x0),
 * `levels` decomposition levels, one layer and 10 bytes of tile data. */
static bytes declared(unsigned ncomps, unsigned levels, unsigned xr, uint32_t x0) {
    bytes b = bytes_copy((const uint8_t *)"\xFF\x4F\xFF\x51", 4);
    uint8_t h[40], c[3] = {7, 0, 0};
    static const uint8_t data[10] = {0};
    unsigned i;
    put16(h, 38 + 3 * ncomps);
    put16(h + 2, 0);
    put32(h + 4, x0 + 100);                     /* Xsiz, Ysiz */
    put32(h + 8, 100);
    put32(h + 12, x0);                          /* XOsiz, YOsiz */
    put32(h + 16, 0);
    put32(h + 20, x0 + 100);                    /* XTsiz, YTsiz */
    put32(h + 24, 100);
    put32(h + 28, 0);                           /* XTOsiz, YTOsiz */
    put32(h + 32, 0);
    put16(h + 36, ncomps);
    append(&b, h, 38);
    c[1] = c[2] = (uint8_t)xr;
    for (i = 0; i < ncomps; i++)
        append(&b, c, 3);
    {
        uint8_t cod[14] = {0xFF, 0x52, 0, 12, 0, 0, 0, 1, 0, 0, 4, 4, 0, 0};
        static const uint8_t qcd[6] = {0xFF, 0x5C, 0, 4, 0x42, 0x40};
        cod[9] = (uint8_t)levels;
        append(&b, cod, 14);
        append(&b, qcd, 6);
    }
    tile_part(&b, 0, 0, 1, data, sizeof data, 0);
    eoc(&b);
    return b;
}

static void test_memory_bounds(void) {
    static const char *too_many = "component and resolution count exceeds supported limit";
    bytes b;
    b = declared(16384, 32, 1, 0);
    expect_error("16,384 components, 33 resolutions", &b, too_many);
    bytes_free(&b);
    b = declared(16384, 32, 255, 1);            /* nearly all of them empty */
    expect_error("16,384 sub-sampled components", &b, too_many);
    bytes_free(&b);
    b = declared(2048, 32, 1, 0);
    expect_error("2,048 components, 33 resolutions", &b, too_many);
    bytes_free(&b);
    b = declared(2048, 31, 1, 0);               /* 65,536: at the limit */
    expect_error("2,048 components, 32 resolutions", &b, "packet count exceeds tile data");
    bytes_free(&b);
}

/* ------------------------------------------------------------------------
 * Optional: a directory of real files
 * ------------------------------------------------------------------------ */

static void test_archive(const char *dirs) {
    char *list = strdup(dirs), *dir, *save = NULL;
    int files = 0;
    for (dir = strtok_r(list, ":", &save); dir != NULL; dir = strtok_r(NULL, ":", &save)) {
        DIR *d = opendir(dir);
        struct dirent *e;
        if (d == NULL) {
            check(0, "cannot open %s", dir);
            continue;
        }
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            char path[4096], error[256];
            bytes file, cs;
            hv_out out;
            if (n < 4 || strcmp(e->d_name + n - 4, ".jp2") != 0)
                continue;
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            file = read_file(path);
            hv_out_init(&out);
            files++;
            if (hv_transcode_file(file.data, file.size, 7, 7, 1, &out, error, sizeof error) != 0) {
                check(0, "%s: rejected: %s", path, error);
            } else {
                bytes result_file = {out.data, out.size};
                expect_served(path, out.data, out.size);
                cs = box_payload(&result_file, HV_BOX_JP2C);
                expect_stable(path, &cs);
            }
            hv_out_free(&out);
            bytes_free(&file);
        }
        closedir(d);
    }
    printf("archive: %d files\n", files);
    free(list);
}

int main(void) {
    struct {
        const char *name;
        void (*run)(void);
    } groups[] = {
        {"Kakadu references", test_references},
        {"tile-parts and packets", test_tile_parts},
        {"main headers", test_headers},
        {"served profile", test_served_profile},
        {"boxes", test_boxes},
        {"corrupted tile data", test_corruption},
        {"SOP, EPH and bit stuffing", test_packet_rules},
        {"memory bounds", test_memory_bounds},
    };
    const char *archive = getenv("TRANSCODE_ARCHIVE");
    size_t i;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < sizeof groups / sizeof groups[0]; i++) {
        int before = failures, n = checks;
        groups[i].run();
        printf("%-28s %5d checks, %d failed\n", groups[i].name, checks - n, failures - before);
    }
    if (archive != NULL && *archive)
        test_archive(archive);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
