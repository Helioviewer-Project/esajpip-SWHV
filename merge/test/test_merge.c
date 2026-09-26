/* test_merge.c: tests of hv_merge_files (merge.c), run by CTest as "merge"
 * (label "tools").
 *
 * MERGE_FIXTURES holds the inputs and hvJP2K's output for them
 * (fixtures/FIXTURES.md); TRANSCODE_FIXTURES the Kakadu references of the
 * transcoder's tests, which are inputs too. */
#define _XOPEN_SOURCE 700

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hv_reader.h"
#include "merge.h"

#ifndef MERGE_FIXTURES
#error "MERGE_FIXTURES must name the fixture directory"
#endif
#ifndef TRANSCODE_FIXTURES
#error "TRANSCODE_FIXTURES must name the transcoder's fixture directory"
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

typedef struct {
    uint8_t *data;
    size_t size;
} bytes;

static void bytes_free(bytes *b) {
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

static bytes read_file(const char *path) {
    bytes b = {NULL, 0};
    FILE *f = fopen(path, "rb");
    long n;
    if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        (b.data = malloc(n ? (size_t)n : 1)) == NULL || fread(b.data, 1, (size_t)n, f) != (size_t)n) {
        check(0, "cannot read %s", path);
        free(b.data);
        b.data = NULL;
    } else {
        b.size = (size_t)n;
    }
    if (f)
        fclose(f);
    return b;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* The inputs of the reference merge, in its order (FIXTURES.md). */
static const char *const names[] = {
    MERGE_FIXTURES "/input/swap_000.jp2",
    MERGE_FIXTURES "/input/swap_001.jp2",
    TRANSCODE_FIXTURES "/kakadu/2015_12_21__00_10_34_34__SDO_AIA_AIA_171.jp2",
    TRANSCODE_FIXTURES "/kakadu/solo_fsi174_127x129_RLCP_PLT.jp2",
    TRANSCODE_FIXTURES "/kakadu/solo_fsi174_509x513_PCRL.jp2",
    TRANSCODE_FIXTURES "/kakadu/solo_fsi174_510x514_LRCP.jp2",
    TRANSCODE_FIXTURES "/kakadu/solo_fsi174_511x513_LRCP_PLT.jp2",
    TRANSCODE_FIXTURES "/kakadu/synthetic_rgb_129x129_CPRL_SOP_EPH.jp2",
};
#define NINPUTS (sizeof names / sizeof *names)

static bytes files[NINPUTS];
static hv_merge_input inputs[NINPUTS];

static void load_inputs(void) {
    size_t i;
    for (i = 0; i < NINPUTS; i++) {
        files[i] = read_file(names[i]);
        inputs[i].path = names[i];
        inputs[i].buf = files[i].data;
        inputs[i].size = files[i].size;
    }
}

/* Merges into memory. 0, or -1 with the message in error. */
static int merge(const hv_merge_input *in, size_t n, int links, bytes *out, char *error,
                 size_t error_size) {
    FILE *f = tmpfile();
    long size;
    int status;
    out->data = NULL;
    out->size = 0;
    if (f == NULL) {
        snprintf(error, error_size, "no temporary file");
        return -1;
    }
    status = hv_merge_files(in, n, links, f, error, error_size);
    if (status == 0 && (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
                        fseek(f, 0, SEEK_SET) != 0 ||
                        (out->data = malloc(size ? (size_t)size : 1)) == NULL ||
                        fread(out->data, 1, (size_t)size, f) != (size_t)size))
        status = -1;
    else if (status == 0)
        out->size = (size_t)size;
    fclose(f);
    return status;
}

/* The JPX file is one the server serves: hv_check_jpx, and every
 * codestream with HV_PROFILE, embedded or linked; and its header boxes are
 * valid (hv_check_jpx_headers). */
static void expect_served(const char *name, const bytes *jpx, const hv_merge_input *in) {
    hv_jpx j;
    size_t at, i;
    const char *error = hv_check_jpx_headers(jpx->data, jpx->size, &at);
    check(error == NULL, "%s: header boxes: %s at %zu", name, error, at);
    error = hv_check_jpx(jpx->data, jpx->size, &j, &at);
    check(error == NULL, "%s: %s at %zu", name, error, at);
    for (i = 0; error == NULL && i < j.count; i++) {
        if (j.jp2c != NULL) {
            error = hv_codestream_check(jpx->data, j.jp2c[i].payload, j.jp2c[i].end, HV_PROFILE, &at);
            check(error == NULL, "%s: codestream %zu: %s at %zu", name, i, error, at);
        } else {
            char path[4096], *real = realpath(in[i].path, NULL);
            check(hv_link_path(&j.links[i], NULL, path, sizeof path) == 0 && real != NULL &&
                  strcmp(path, real) == 0, "%s: link %zu does not name %s", name, i, in[i].path);
            error = hv_check_link(in[i].buf, in[i].size, &j.links[i], &at);
            check(error == NULL, "%s: link %zu: %s at %zu", name, i, error, at);
            free(real);
        }
    }
    if (error == NULL)
        hv_jpx_free(&j);
}

/* The standard features of a Reader Requirements box, in order. */
static int rreq_features(const bytes *jpx, int *features, int max) {
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at;
    int k = 0;
    hv_boxes_file(&it, jpx->data, jpx->size);
    while (hv_boxes_next(&it, &box, &message, &at) == 1)
        if (box.type == HV_BOX_RREQ) {
            const uint8_t *p = jpx->data + box.payload;
            int ml = p[0], nsf = p[1 + 2 * ml] << 8 | p[2 + 2 * ml], i;
            for (i = 0; i < nsf && k < max; i++)
                features[k++] = p[3 + 2 * ml + i * (2 + ml)] << 8 | p[4 + 2 * ml + i * (2 + ml)];
            break;
        }
    return k;
}

static void expect_features(const char *name, const bytes *jpx, const int *expected, int n) {
    int got[32], k = rreq_features(jpx, got, 32);
    check(k == n && memcmp(got, expected, (size_t)n * sizeof *got) == 0,
          "%s: rreq lists %d features, expected %d", name, k, n);
}

/* ------------------------------------------------------------------------ */

/* hvJP2K's output, byte for byte, and served. */
static void test_reference(void) {
    bytes expected = read_file(MERGE_FIXTURES "/expected/merged.jpx"), got;
    char error[512];
    if (merge(inputs, NINPUTS, 0, &got, error, sizeof error) != 0) {
        check(0, "reference merge: %s", error);
    } else {
        check(got.size == expected.size && memcmp(got.data, expected.data, got.size) == 0,
              "reference merge differs from hvJP2K's");
        expect_served("reference merge", &got, inputs);
    }
    bytes_free(&got);
    bytes_free(&expected);
}

/* The linked merge: served, its links naming the inputs, and box for box
 * the embedded one, with the codestreams as fragment tables, jpx the only
 * compatible brand, feature 15, and a final Data Reference box. */
static void test_links(void) {
    bytes embedded, linked;
    hv_boxes a, b;
    hv_box x, y;
    const char *message;
    char error[512];
    size_t at, n = 0;
    int more_a, more_b;
    static const int features[] = {1, 2, 5, 15};

    if (merge(inputs, NINPUTS, 0, &embedded, error, sizeof error) != 0 ||
        merge(inputs, NINPUTS, 1, &linked, error, sizeof error) != 0) {
        check(0, "linked merge: %s", error);
        return;
    }
    expect_served("linked merge", &linked, inputs);
    expect_features("linked merge", &linked, features, 4);
    hv_boxes_file(&a, embedded.data, embedded.size);
    hv_boxes_file(&b, linked.data, linked.size);
    for (;;) {
        more_a = hv_boxes_next(&a, &x, &message, &at) == 1;
        more_b = hv_boxes_next(&b, &y, &message, &at) == 1;
        if (!more_a)
            break;
        check(more_b, "linked merge: box %zu missing", n);
        if (!more_b)
            break;
        if (x.type == HV_BOX_JP2C) {
            check(y.type == HV_BOX_FTBL, "linked merge: box %zu is not an ftbl", n);
        } else if (x.type == HV_BOX_FTYP) {
            check(y.end - y.payload == 12 && get32(linked.data + y.payload + 8) == HV_BRAND_JPX,
                  "linked merge: ftyp does not list jpx alone");
        } else if (x.type != HV_BOX_RREQ) {
            check(x.end - x.start == y.end - y.start &&
                  memcmp(embedded.data + x.start, linked.data + y.start, x.end - x.start) == 0,
                  "linked merge: box %zu differs from the embedded merge", n);
        }
        n++;
    }
    check(more_b && y.type == HV_BOX_DTBL && hv_boxes_next(&b, &y, &message, &at) == 0,
          "linked merge: does not end with one dtbl");
    bytes_free(&embedded);
    bytes_free(&linked);
}

/* hvJP2K's reader_requirements cases: Rsiz 0, 1 and 2, and opacity
 * channels (cdef types 1 and 2) in two linked inputs. */
/* A copy of a JP2 file with n bytes of boxes added at the end of its JP2
 * Header box. */
static bytes add_to_jp2h(const bytes *jp2, const uint8_t *boxes, size_t n) {
    bytes out = {malloc(jp2->size + n), jp2->size + n};
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at;
    memcpy(out.data, jp2->data, jp2->size);
    hv_boxes_file(&it, jp2->data, jp2->size);
    while (hv_boxes_next(&it, &box, &message, &at) == 1)
        if (box.type == HV_BOX_JP2H) {
            memcpy(out.data + box.end, boxes, n);
            memcpy(out.data + box.end + n, jp2->data + box.end, jp2->size - box.end);
            put32(out.data + box.start, (uint32_t)(box.end - box.start + n));
            break;
        }
    return out;
}

static void expect_error(const char *name, const hv_merge_input *in, size_t n, int links,
                         const char *message);

static void test_rreq(void) {
    static const int only[] = {1}, rsiz2[] = {1, 4}, rsiz0[] = {1, 5}, all[] = {1, 2, 5, 9, 10, 15};
    /* Channel 0 described as opacity and as premultiplied opacity. */
    static const uint8_t cdef[22] = {0, 0, 0, 22, 'c', 'd', 'e', 'f', 0, 2,
                                     0, 0, 0, 1, 0, 0, 0, 0, 0, 2, 0, 0};
    bytes jp2 = {NULL, 0}, opacity, out;
    hv_merge_input in[2];
    hv_box jp2c;
    char error[512];
    size_t at;
    int rsiz;

    if (files[0].data == NULL || hv_check_jp2(files[0].data, files[0].size, &jp2c, &at) != NULL)
        return;
    jp2.data = malloc(files[0].size);
    memcpy(jp2.data, files[0].data, files[0].size);
    jp2.size = files[0].size;
    in[0].path = names[0];
    in[0].buf = jp2.data;
    in[0].size = jp2.size;
    for (rsiz = 0; rsiz < 3; rsiz++) {
        jp2.data[jp2c.payload + 7] = (uint8_t)rsiz;
        if (merge(in, 1, 0, &out, error, sizeof error) != 0) {
            check(0, "Rsiz %d: %s", rsiz, error);
            continue;
        }
        if (rsiz == 0)
            expect_features("Rsiz 0", &out, rsiz0, 2);
        else if (rsiz == 1)
            expect_features("Rsiz 1", &out, only, 1);
        else
            expect_features("Rsiz 2", &out, rsiz2, 2);
        bytes_free(&out);
    }

    /* A cdef box at the end of the JP2 Header box. */
    jp2.data[jp2c.payload + 7] = 0;
    opacity = add_to_jp2h(&jp2, cdef, sizeof cdef);
    in[0].buf = opacity.data;
    in[0].size = opacity.size;
    in[1] = in[0];
    if (merge(in, 2, 1, &out, error, sizeof error) != 0) {
        check(0, "opacity: %s", error);
    } else {
        expect_features("opacity, linked", &out, all, 6);
        bytes_free(&out);
    }
    /* A later input without the cdef of the first would inherit it. */
    in[1].buf = jp2.data;
    in[1].size = jp2.size;
    expect_error("no cdef after one", in, 2, 0, "no cdef box, but the first input has one");
    bytes_free(&opacity);
    bytes_free(&jp2);
}

/* An input whose JP2 Header box its codestream contradicts: NC in the
 * ihdr of a one-component input after one with a palette. hv_merge once
 * sized that input's generated cmap by an NC of up to 65 535 and wrote
 * past the buffer; the reference merge covers the generated cmap itself. */
static void test_headers(void) {
    /* A Resolution box holding a Capture Resolution box of 1/1 per metre. */
    static const uint8_t res[26] = {0, 0, 0, 26, 'r', 'e', 's', ' ', 0, 0, 0, 18, 'r', 'e', 's', 'c',
                                    0, 1, 0, 1, 0, 1, 0, 1, 0, 0};
    bytes bad = {NULL, 0};
    hv_merge_input in[2];
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at;

    if (files[0].data == NULL || files[2].data == NULL)
        return;
    bad.data = malloc(files[2].size);
    memcpy(bad.data, files[2].data, files[2].size);
    bad.size = files[2].size;
    hv_boxes_file(&it, bad.data, bad.size);
    while (hv_boxes_next(&it, &box, &message, &at) == 1)
        if (box.type == HV_BOX_JP2H)
            break;
    in[0] = inputs[0];
    in[1] = inputs[2];
    in[1].buf = bad.data;
    in[1].size = bad.size;
    put16(bad.data + box.payload + 16, 65535);          /* ihdr NC: above Ihdr's 16 384 */
    expect_error("NC 65 535", in, 2, 0, "ihdr: not 14 bytes of valid fields");
    put16(bad.data + box.payload + 16, 16384);          /* for Csiz 1 */
    expect_error("NC 16 384", in, 2, 0, "ihdr.nc");
    bytes_free(&bad);

    /* A later input without the res of the first would inherit it. */
    bad = add_to_jp2h(&files[0], res, sizeof res);
    in[0].buf = bad.data;
    in[0].size = bad.size;
    in[1] = inputs[0];
    expect_error("no res after one", in, 2, 0, "no res box, but the first input has one");
    bytes_free(&bad);
}

static void expect_error(const char *name, const hv_merge_input *in, size_t n, int links,
                         const char *message) {
    bytes out;
    char error[512];
    int status = merge(in, n, links, &out, error, sizeof error);
    check(status != 0 && strstr(error, message) != NULL, "%s: \"%s\", expected \"%s\"", name,
          status ? error : "accepted", message);
    bytes_free(&out);
}

/* Inputs the served profile excludes, and what the merge itself needs. */
static void test_errors(void) {
    bytes origin = read_file(TRANSCODE_FIXTURES "/input/synthetic_rgb_129x129_origin129_CPRL.jp2");
    hv_merge_input in = {TRANSCODE_FIXTURES "/input/synthetic_rgb_129x129_origin129_CPRL.jp2",
                         origin.data, origin.size};
    bytes no_jp2h = {NULL, 0};
    hv_boxes it;
    hv_box box;
    const char *message;
    char dir[] = "/tmp/test_merge.XXXXXX", path[256];
    size_t at;
    FILE *f;

    expect_error("no inputs", inputs, 0, 0, "no JP2 input files");
    expect_error("nonzero origins", &in, 1, 0, "siz.zero-origin");

    /* The first input without its JP2 Header box. */
    no_jp2h.data = malloc(files[0].size);
    hv_boxes_file(&it, files[0].data, files[0].size);
    while (hv_boxes_next(&it, &box, &message, &at) == 1)
        if (box.type == HV_BOX_JP2H) {
            memcpy(no_jp2h.data, files[0].data, box.start);
            memcpy(no_jp2h.data + box.start, files[0].data + box.end, files[0].size - box.end);
            no_jp2h.size = files[0].size - (box.end - box.start);
        }
    in.path = names[0];
    in.buf = no_jp2h.data;
    in.size = no_jp2h.size;
    expect_error("no jp2h", &in, 1, 0, "jp2.one-jp2h");

    /* A link to a file the server would not open. */
    if (mkdtemp(dir) != NULL) {
        snprintf(path, sizeof path, "%s/frame.j2k", dir);
        if ((f = fopen(path, "wb")) != NULL) {
            fwrite(files[0].data, 1, files[0].size, f);
            fclose(f);
            in = inputs[0];
            in.path = path;
            expect_error("link to a .j2k", &in, 1, 1, "url.jp2-target");
            unlink(path);
        }
        rmdir(dir);
    }
    bytes_free(&no_jp2h);
    bytes_free(&origin);
}

int main(void) {
    struct {
        const char *name;
        void (*run)(void);
    } groups[] = {
        {"hvJP2K reference", test_reference},
        {"linked merge", test_links},
        {"reader requirements", test_rreq},
        {"header boxes", test_headers},
        {"rejected inputs", test_errors},
    };
    size_t i;
    load_inputs();
    for (i = 0; i < sizeof groups / sizeof groups[0]; i++) {
        int before = failures, n = checks;
        groups[i].run();
        printf("%-28s %5d checks, %d failed\n", groups[i].name, checks - n, failures - before);
    }
    for (i = 0; i < NINPUTS; i++)
        bytes_free(&files[i]);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
