/* test_profile: the reader's checks against the corpus in
 * tests/vectors/j2k, and unit checks of the reader and rules.
 *
 * Served profile. The reader and the model's harness share the rules
 * (lib/hv_rules.c), so the reader's profile checks (hv_check_served:
 * hv_check_jp2 or hv_check_jpx, then every codestream with HV_PROFILE,
 * embedded or in the linked files) must accept a vector exactly when the
 * manifest labels it profile-valid.
 *
 * Header boxes. hv_check_jp2h and hv_check_jpx_headers check the standard
 * layer, but only part of it: the header boxes, the placement of the
 * top-level superboxes' children, the JPX count rules and each
 * codestream's SIZ against its header. The contract, taken from the
 * manifest's labels and the checks' own answers, not from vector names:
 *   (a) they accept every standard-valid vector;
 *   (b) a vector they reject is standard-invalid, by the rule they name
 *       (any error where the manifest's reason is "decode");
 *   (c) a rule they name for one vector they name for every vector the
 *       manifest gives that reason;
 *   (d) they reject every vector that is standard-invalid but
 *       profile-valid: the profile keeps the header boxes and the JP2
 *       file's other boxes opaque, so those are the rules of the header
 *       checks, except for the two leniencies of the profile that the
 *       header checks do not read (LENIENCIES below).
 *
 *   test_profile <vector directory> */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_reader.h"
#include "hv_served.h"

static int failures;

static void check(int ok, const char *what, const char *detail) {
    if (!ok) {
        printf("FAIL %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
        failures++;
    }
}

/* NULL if the JP2 file passes hv_check_jp2 and its codestream reads
 * through with `flags`, otherwise the error and its offset. */
static const char *profile_error(const uint8_t *buf, size_t size, unsigned flags) {
    static char message[256];
    hv_box jp2c;
    size_t at = 0;
    const char *error = hv_check_jp2(buf, size, &jp2c, &at);
    if (error == NULL)
        error = hv_codestream_check(buf, jp2c.payload, jp2c.end, flags, &at);
    if (error == NULL)
        return NULL;
    snprintf(message, sizeof message, "%s at %zu", error, at);
    return message;
}

static uint8_t *vector(const char *dir, const char *name, size_t *size) {
    char path[4096];
    uint8_t *buf;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if ((buf = hv_load_file(path, size)) == NULL)
        check(0, "cannot read", path);
    return buf;
}

/* A malloc'd copy of s (strdup is not ISO C). */
static char *copy(const char *s) {
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    return c ? memcpy(c, s, n) : NULL;
}

/* One manifest row, and what the header checks said of it. */
typedef struct {
    char *name, *standard, *profile, *reason, *field;
    const char *header;         /* the header checks' error, or NULL */
    size_t at;
} row;

/* The profile's leniencies that the header checks do not read, which (d)
 * leaves out: by reason, or by the box of the field. */
static const struct {
    const char *reason, *field_box;
} LENIENCIES[] = {
    {"plt.zero-length", NULL},  /* PLT padding: the codestream reader's */
    {NULL, "asoc."},            /* asoc contents: opaque to the reader */
    {NULL, "main.marker-code"}, /* unknown main-header marker: the codestream reader's */
};

static int lenient(const row *r) {
    size_t i;
    for (i = 0; i < sizeof LENIENCIES / sizeof *LENIENCIES; i++)
        if ((LENIENCIES[i].reason && strcmp(r->reason, LENIENCIES[i].reason) == 0) ||
            (LENIENCIES[i].field_box &&
             strncmp(r->field, LENIENCIES[i].field_box, strlen(LENIENCIES[i].field_box)) == 0))
            return 1;
    return 0;
}

/* (a) to (d) above, over every row. Returns the rows the header checks
 * reject by the manifest's reason, "decode" included. */
static int check_header_contract(const row *rows, size_t n) {
    char detail[8192];
    size_t i, j;
    int rejected = 0;

    for (i = 0; i < n; i++) {
        const row *r = &rows[i];
        int decode = strcmp(r->reason, "decode") == 0, named = 1;
        snprintf(detail, sizeof detail, "%s: header checks %s at %zu, manifest %s/%s (%s)",
                 r->name, r->header ? r->header : "valid", r->at, r->standard, r->profile,
                 r->reason);
        if (strcmp(r->standard, "valid") == 0) {
            check(r->header == NULL, "(a) header checks accept standard-valid", detail);
            continue;
        }
        if (r->header != NULL) {
            check(decode || strcmp(r->header, r->reason) == 0, "(b) header rule name", detail);
            rejected += decode || strcmp(r->header, r->reason) == 0;
        }
        /* (c): r's reason, if the checks name it for another vector. */
        for (j = 0; j < n && !decode && r->header == NULL; j++)
            if (rows[j].header != NULL && strcmp(rows[j].header, r->reason) == 0 &&
                strcmp(rows[j].reason, r->reason) == 0) {
                named = 0;
                break;
            }
        check(named, "(c) header rule fires for every vector of its reason", detail);
        if (strcmp(r->profile, "valid") == 0 && !lenient(r))
            check(r->header != NULL, "(d) header checks reject standard-only invalid", detail);
    }
    return rejected;
}

/* The corpus: every row of the manifest. */
static void check_corpus(const char *dir) {
    char path[4096], line[8192];
    FILE *manifest;
    row *rows = NULL;
    size_t nrows = 0, cap = 0, i;
    int jp2 = 0, jpx = 0, valid = 0, header_invalid;

    snprintf(path, sizeof path, "%s/manifest.tsv", dir);
    if ((manifest = fopen(path, "r")) == NULL) {
        check(0, "cannot read", path);
        return;
    }
    check(fgets(line, sizeof line, manifest) != NULL &&
          strncmp(line, "file\tkind\tstandard\tprofile\treason\tfield\t", 40) == 0,
          "manifest header", NULL);
    while (fgets(line, sizeof line, manifest) != NULL) {
        char *field[6], *p = line, detail[8192];
        const char *error;
        uint8_t *buf;
        size_t size;
        hv_served served;
        row *r;
        int k, expected, is_jpx;

        for (k = 0; k < 6; k++) {
            field[k] = p;
            if ((p = strchr(p, '\t')) == NULL)
                break;
            *p++ = 0;
        }
        if (k < 6) {
            check(0, "manifest row", line);
            continue;
        }
        expected = strcmp(field[3], "valid") == 0;
        is_jpx = strcmp(field[1], "jpx") == 0;
        if ((buf = vector(dir, field[0], &size)) == NULL)
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, field[0]);
        error = hv_check_served(path, buf, size, is_jpx, &served);
        jpx += is_jpx;
        jp2 += !is_jpx;
        snprintf(detail, sizeof detail, "%s: reader %s at %zu%s%s, manifest %s (%s)", field[0],
                 error ? error : "valid", served.at, served.linked_path[0] ? " of " : "",
                 served.linked_path, field[3], field[4]);
        check((error == NULL) == expected, "profile label", detail);
        valid += expected;

        if (nrows == cap) {
            row *grown = realloc(rows, (cap = cap ? 2 * cap : 256) * sizeof *rows);
            if (grown == NULL) {
                check(0, "out of memory", NULL);
                free(buf);
                break;
            }
            rows = grown;
        }
        r = &rows[nrows++];
        r->name = copy(field[0]);
        r->standard = copy(field[2]);
        r->profile = copy(field[3]);
        r->reason = copy(field[4]);
        r->field = copy(field[5]);
        if (!r->name || !r->standard || !r->profile || !r->reason || !r->field) {
            check(0, "out of memory", NULL);
            nrows--;
            free(buf);
            break;
        }
        r->at = 0;
        r->header = is_jpx ? hv_check_jpx_headers(buf, size, &r->at)
                           : hv_check_jp2h(buf, size, &r->at);
        free(buf);
    }
    fclose(manifest);
    header_invalid = check_header_contract(rows, nrows);
    printf("%d JP2 and %d JPX vectors, %d profile-valid, %d invalid by the header checks\n", jp2,
           jpx, valid, header_invalid);
    check(jp2 > 0 && jpx > 0 && valid > 0 && header_invalid > 0,
          "corpus has JP2 and JPX vectors, some valid, some with invalid headers", NULL);
    for (i = 0; i < nrows; i++) {
        free(rows[i].name);
        free(rows[i].standard);
        free(rows[i].profile);
        free(rows[i].reason);
        free(rows[i].field);
    }
    free(rows);
}

/* HV_PROFILE_HEADERS: the main header only. */
static void check_headers_scope(const char *dir) {
    static const struct {
        const char *name;
        const char *error;      /* expected with HV_PROFILE_HEADERS; NULL: accepted */
    } cases[] = {
        {"jp2-cod.scod.sopMarkers-1.jp2", NULL},
        {"jp2-rule-codestream.no-plt-4.jp2", NULL},
        {"jp2-siz.xtsiz-3.jp2", "siz.single-tile"},
        {"jp2-siz.xosiz-1.jp2", "siz.zero-origin"},
        {"jp2-rule-main.packet-headers-moved-48.jp2",
         "main.marker-code"},
        {"jp2-main-ff30-length.jp2", "main.marker-code"},
        {"jp2-main-ppt.jp2", "main.marker-code"},
    };
    size_t i, size;
    for (i = 0; i < sizeof cases / sizeof *cases; i++) {
        uint8_t *buf = vector(dir, cases[i].name, &size);
        const char *error;
        if (buf == NULL)
            continue;
        error = profile_error(buf, size, HV_PROFILE_HEADERS);
        if (cases[i].error == NULL)
            check(error == NULL, cases[i].name, error);
        else
            check(error != NULL && strncmp(error, cases[i].error, strlen(cases[i].error)) == 0,
                  cases[i].name, error ? error : "accepted");
        check(profile_error(buf, size, HV_PROFILE) != NULL, cases[i].name,
              "accepted with HV_PROFILE");
        free(buf);
    }
}

/* hv_check_jp2 on inputs that are not JP2 files. */
static void check_file_rules(const char *dir) {
    size_t size, at, cs;
    hv_box jp2c;
    uint8_t *buf = vector(dir, "jp2.jp2", &size);
    const char *error;
    if (buf == NULL)
        return;
    check(hv_check_jp2(buf, size, &jp2c, &at) == NULL, "jp2.jp2", NULL);
    /* The size rule is checked before the buffer is read. */
    error = hv_check_jp2(buf, (size_t)INT_MAX + 1, &jp2c, &at);
    check(error != NULL && strcmp(error, "file.size-limit") == 0, "INT_MAX + 1 bytes", error);
    /* The raw codestream of jp2.jp2. */
    hv_check_jp2(buf, size, &jp2c, &at);
    cs = jp2c.payload;
    error = hv_check_jp2(buf + cs, jp2c.end - cs, &jp2c, &at);
    check(error != NULL && strcmp(error, "file.signature") == 0, "raw codestream", error);
    free(buf);
}

/* hv_check_jpx's rule names where a later check could claim the file. */
static void check_jpx_rules(const char *dir) {
    static const struct {
        const char *name, *error;
    } cases[] = {
        {"jpx-linked-rule-jpx.one-dtbl-11.jpx", "jpx.one-dtbl"},
        {"jpx-nested-jpch.jpx", "box.nested-superbox"},
    };
    size_t i, size, at;
    for (i = 0; i < sizeof cases / sizeof *cases; i++) {
        uint8_t *buf = vector(dir, cases[i].name, &size);
        const char *error;
        hv_jpx jpx;
        if (buf == NULL)
            continue;
        error = hv_check_jpx(buf, size, &jpx, &at);
        check(error != NULL && strcmp(error, cases[i].error) == 0, cases[i].name,
              error ? error : "accepted");
        if (error == NULL)
            hv_jpx_free(&jpx);
        free(buf);
    }
}

/* The box of `type` that starts at `at` among the top-level boxes of buf
 * and, when `parent` is nonzero, the children of the top-level `parent`
 * boxes. */
static int box_at(const uint8_t *buf, size_t size, uint32_t parent, uint32_t type, size_t at) {
    hv_boxes it, children;
    hv_box box, child;
    const char *error;
    size_t where;
    hv_boxes_file(&it, buf, size);
    while (hv_boxes_next(&it, &box, &error, &where) == 1) {
        if (box.type == type && box.start == at)
            return 1;
        if (parent == 0 || box.type != parent)
            continue;
        hv_boxes_children(&children, buf, &box);
        while (hv_boxes_next(&children, &child, &error, &where) == 1)
            if (child.type == type && child.start == at)
                return 1;
    }
    return 0;
}

/* Where the reader's errors point. */
static void check_offsets(const char *dir) {
    size_t size, at = 0;
    uint8_t *buf;
    const char *error;
    hv_jpx jpx;

    /* A header rule comparing boxes: at the header box. */
    if ((buf = vector(dir, "jp2-header-ihdr.width-20.jp2", &size)) != NULL) {
        error = hv_check_jp2h(buf, size, &at);
        check(error != NULL && strcmp(error, "ihdr.width") == 0 &&
              box_at(buf, size, 0, HV_BOX_JP2H, at), "ihdr.width at jp2h", error);
        free(buf);
    }
    /* A fragment's DR: at its flst box. */
    if ((buf = vector(dir, "jpx-linked-rule-flst.dr-range-3.jpx", &size)) != NULL) {
        error = hv_check_jpx(buf, size, &jpx, &at);
        check(error != NULL && strcmp(error, "flst.dr-range") == 0 &&
              box_at(buf, size, HV_BOX_FTBL, HV_BOX_FLST, at), "flst.dr-range at flst", error);
        if (error == NULL)
            hv_jpx_free(&jpx);
        free(buf);
    }
}

/* A second jp2h in a JPX file (T.801 M.11.5): jpx.one-jp2h. */
static void check_second_jp2h(const char *dir) {
    size_t size, at = 0;
    uint8_t *buf = vector(dir, "jpx-embedded.jpx", &size), *two;
    const char *error;
    hv_boxes it;
    hv_box box, jp2h = {0};
    int found = 0;

    if (buf == NULL)
        return;
    hv_boxes_file(&it, buf, size);
    while (!found && hv_boxes_next(&it, &box, &error, &at) == 1)
        if (box.type == HV_BOX_JP2H) {
            jp2h = box;
            found = 1;
        }
    check(found, "jpx-embedded.jpx has a jp2h", NULL);
    if (found && (two = malloc(size + (jp2h.end - jp2h.start))) != NULL) {
        size_t n = jp2h.end - jp2h.start;
        memcpy(two, buf, jp2h.end);
        memcpy(two + jp2h.end, buf + jp2h.start, n);
        memcpy(two + jp2h.end + n, buf + jp2h.end, size - jp2h.end);
        error = hv_check_jpx_headers(two, size + n, &at);
        check(error != NULL && strcmp(error, "jpx.one-jp2h") == 0, "two jp2h in a JPX file",
              error ? error : "accepted");
        free(two);
    }
    free(buf);
}

/* The accessors and items over a codestream's life. */
static void check_codestream_lifetime(const char *dir) {
    size_t size;
    uint8_t *buf = vector(dir, "jp2.jp2", &size);
    hv_codestream cs;
    hv_item item;
    hv_box jp2c;
    size_t at, data = 0, plts = 0;
    uint64_t plt_sum = 0;
    int status, sot_ok = 1, siz_ok = 0, plt_ok = 1;

    /* start after end: refused, and nothing to read. */
    check(hv_codestream_open(&cs, (const uint8_t *)"", 1, 0, 0) != 0 && cs.error != NULL &&
          hv_codestream_siz(&cs) == NULL && hv_codestream_next(&cs, &item) < 0,
          "codestream start after end", cs.error);
    hv_codestream_close(&cs);
    if (buf == NULL)
        return;
    if (hv_check_jp2(buf, size, &jp2c, &at) != NULL) {
        check(0, "jp2.jp2", "hv_check_jp2");
        free(buf);
        return;
    }
    check(hv_codestream_open(&cs, buf, jp2c.payload, jp2c.end, HV_PROFILE) == 0 &&
          hv_codestream_siz(&cs) != NULL && hv_codestream_cod(&cs) == NULL &&
          hv_codestream_qcd(&cs) == NULL, "accessors after open", cs.error);
    while ((status = hv_codestream_next(&cs, &item)) == 1) {
        int tile = item.kind == HV_TILE_PART || item.kind == HV_TILE_SEGMENT ||
                   item.kind == HV_TILE_DATA;
        sot_ok &= tile ? item.sot.lsot != 0 : item.sot.lsot == 0 && item.sot.psot == 0;
        if (item.siz != NULL)
            siz_ok = item.siz == hv_codestream_siz(&cs) &&
                     item.siz->ncomponents == item.siz->fixed->csiz;
        if (item.plt != NULL) {
            /* The entries, in place after Zplt, to the end of the segment. */
            size_t pos = item.plt->start;
            uint64_t v;
            while (hv_plt_next(buf, &pos, item.plt->end, &v) == 1)
                plt_sum += v;
            plts++;
            plt_ok &= pos == item.end && item.plt->end == item.end &&
                      item.plt->start == item.start + 5;
        }
        if (item.kind == HV_TILE_DATA)
            data += item.end - item.start;
    }
    check(status == 0, "jp2.jp2 reads through", cs.error);
    check(sot_ok, "hv_item.sot only on tile-part items", NULL);
    check(siz_ok, "hv_item.siz: the accessor's, with Csiz components", NULL);
    check(plts > 0 && plt_ok && plt_sum == data, "hv_item.plt: the entries cover the data",
          NULL);
    check(hv_codestream_cod(&cs) != NULL && hv_codestream_qcd(&cs) != NULL,
          "COD and QCD accessors after the main header", NULL);
    hv_codestream_close(&cs);
    check(hv_codestream_siz(&cs) == NULL && hv_codestream_cod(&cs) == NULL &&
          hv_codestream_qcd(&cs) == NULL, "accessors after close", NULL);
    free(buf);
}

/* hv_plt_next on entries the corpus does not hold: the end, a value in
 * ten bytes, and the entries it refuses (truncated, eleven bytes, above
 * 64 bits), which leave *pos where it was. */
static void check_plt_next(void) {
    static const uint8_t two[] = {0x81, 0x00};
    static const uint8_t truncated[] = {0x81};
    static const uint8_t top[] = {0x81, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
    static const uint8_t over[] = {0x82, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
    static const uint8_t eleven[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                                     0x81, 0x00};
    size_t pos = 0;
    uint64_t v = 0;

    check(hv_plt_next(two, &pos, sizeof two, &v) == 1 && v == 128 && pos == 2 &&
          hv_plt_next(two, &pos, sizeof two, &v) == 0 && pos == 2, "PLT entry of two bytes",
          NULL);
    pos = 0;
    check(hv_plt_next(top, &pos, sizeof top, &v) == 1 && v == (uint64_t)1 << 63 &&
          pos == sizeof top, "PLT entry of 2^63 in ten bytes", NULL);
    pos = 0;
    check(hv_plt_next(truncated, &pos, sizeof truncated, &v) == -1 && pos == 0,
          "truncated PLT entry", NULL);
    check(hv_plt_next(over, &pos, sizeof over, &v) == -1 && pos == 0,
          "PLT entry above 64 bits", NULL);
    check(hv_plt_next(eleven, &pos, sizeof eleven, &v) == -1 && pos == 0,
          "PLT entry of eleven bytes", NULL);
}

static void check_link_paths(void) {
    static const struct {
        const char *loc, *expected;
    } valid[] = {
        {"file://%2Ftmp/a.jp2", "/tmp/a.jp2"},
        {"file://a%20b.jp2", "d/a b.jp2"},
        {"file://localhost/a.jp2", "d/localhost/a.jp2"},
    };
    static const struct {
        const char *loc, *error;
    } invalid[] = {
        {"file://%00.jp2", "url.percent-encoding"},
        {"file://%4.jp2", "url.percent-encoding"},
        {"file://", "url.length"},
        {"http://a.jp2", "url.file-scheme"},
    };
    hv_link link = {0};
    char out[64];
    const char *error;
    size_t i;

    for (i = 0; i < sizeof valid / sizeof *valid; i++) {
        link.loc = (const uint8_t *)valid[i].loc;
        link.loc_size = strlen(valid[i].loc);
        check(hv_link_path(&link, "d/x.jpx", out, sizeof out) == NULL &&
              strcmp(out, valid[i].expected) == 0, "link path", valid[i].loc);
    }
    link.loc = (const uint8_t *)valid[1].loc;
    link.loc_size = strlen(valid[1].loc);
    check(hv_link_path(&link, NULL, out, sizeof out) == NULL && strcmp(out, "a b.jp2") == 0,
          "link path without a JPX path", valid[1].loc);
    for (i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        link.loc = (const uint8_t *)invalid[i].loc;
        link.loc_size = strlen(invalid[i].loc);
        memset(out, 'X', sizeof out);
        error = hv_link_path(&link, "d/x.jpx", out, sizeof out);
        check(error != NULL && strcmp(error, invalid[i].error) == 0 && out[0] == 0,
              "invalid link path", invalid[i].loc);
    }
    link.loc = (const uint8_t *)valid[1].loc;
    link.loc_size = strlen(valid[1].loc);
    memset(out, 'X', sizeof out);
    check(hv_link_path(&link, "d/x.jpx", out, 4) != NULL && out[0] == 0,
          "link path too long", valid[1].loc);
}

/* hv_rule_url on LOC with its NUL. */
static const char *url_rule(const char *loc) {
    return hv_rule_url(0, 0, (const uint8_t *)loc, strlen(loc) + 1);
}

static void check_url_rule(void) {
    static const struct {
        const char *loc, *error;
    } cases[] = {
        {"file://a.jp2", NULL},
        {"file://a%20b.jp2", NULL},
        {"file://", "url.length"},
        {"http://a.jp2", "url.file-scheme"},
        {"file://a%2.jp2", "url.percent-encoding"},
        {"file://a%00.jp2", "url.percent-encoding"},
        {"file://a.jpx", "url.jp2-target"},
    };
    size_t i;
    for (i = 0; i < sizeof cases / sizeof *cases; i++) {
        const char *error = url_rule(cases[i].loc);
        check(cases[i].error ? error != NULL && strcmp(error, cases[i].error) == 0
                             : error == NULL, cases[i].loc, error ? error : "accepted");
    }
}

static void check_long_url(void) {
    uint8_t loc[1026];
    memset(loc, 'a', sizeof loc);
    memcpy(loc, "file://", HV_FILE_SCHEME_LENGTH);
    memcpy(loc + 1019, ".jp2", 4);
    loc[1023] = 0;
    check(hv_rule_url(0, 0, loc, 1024) == NULL, "1,023-character URL", NULL);
    memcpy(loc + 1020, ".jp2", 4);
    loc[1024] = 0;
    check(hv_rule_url(0, 0, loc, 1025) == NULL, "1,024-character URL", NULL);
    memcpy(loc + 1021, ".jp2", 4);
    loc[1025] = 0;
    check(hv_rule_url(0, 0, loc, 1026) == NULL, "1,025-character URL", NULL);
}

static void check_jpx_name(void) {
    check(hv_is_jpx_name("a.jpx") && hv_is_jpx_name("a.JPX") && hv_is_jpx_name("d/a.Jpx") &&
          !hv_is_jpx_name("a.jp2") && !hv_is_jpx_name("jpx") && !hv_is_jpx_name(""),
          "JPX file names", NULL);
}

/* hv_rules.c functions whose inputs the corpus cannot reach. */
static void check_rules(void) {
    static hv_header h;
    static CdefEntry entries[65535];
    const char *error;
    size_t i;

    /* cdef: any number of entries, without allocating. */
    hv_header_init(&h, HV_BOX_JP2H, 0);
    h.cdef = 1;
    for (i = 0; i < 65535; i++) {
        entries[i].cn = i;
        entries[i].typ = i % 3;
        entries[i].asoc = i / 3;
    }
    check(hv_rule_cdef(&h, entries, 65535) == NULL && h.cdef_cn == 65534,
          "65,535 distinct cdef entries", NULL);
    entries[65534].typ = entries[0].typ;
    entries[65534].asoc = entries[0].asoc;
    error = hv_rule_cdef(&h, entries, 65535);
    check(error != NULL && strcmp(error, "cdef.pairs") == 0, "repeated cdef pair", error);
    entries[65534].typ = 65535;
    check(hv_rule_cdef(&h, entries, 65535) == NULL, "unspecified Typ repeats", NULL);
    entries[65534].typ = entries[0].typ;
    entries[65534].asoc = 65535;
    entries[65533].typ = entries[0].typ;
    entries[65533].asoc = 65535;
    check(hv_rule_cdef(&h, entries, 65535) == NULL, "unspecified Asoc repeats", NULL);

    /* hv_header_init clears what an earlier header left. */
    h.cmap_count = h.cdef_cn = h.npc = 1;
    h.resc = h.resd = h.cmap_palette = 1;
    hv_header_init(&h, HV_BOX_JPCH, 1);
    check(h.cmap_count == 0 && h.cdef_cn == 0 && h.npc == 0 && h.resc == 0 && h.resd == 0 &&
          h.cmap_palette == 0 && h.children == 0 && h.parent == HV_BOX_JPCH && h.jpx == 1,
          "hv_header_init", NULL);

    /* Where jp2h is. */
    error = hv_rule_jp2h_place(1, 0, 0, 0);
    check(error != NULL && strcmp(error, "jp2.one-codestream") == 0, "JP2 without jp2c", error);
    error = hv_rule_jp2h_place(2, 0, 0, 1);
    check(error != NULL && strcmp(error, "jpx.one-jp2h") == 0, "JPX with two jp2h", error);
    check(hv_rule_jp2h_place(0, 0, 0, 1) == NULL, "JPX without jp2h", NULL);

    check(hv_rule_tile_part_count(HV_PROFILE_TILE_PARTS) == NULL &&
          hv_rule_tile_part_count(HV_PROFILE_TILE_PARTS + 1) != NULL, "tile-part limit", NULL);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: test_profile <vector directory>\n");
        return 2;
    }
    check_corpus(argv[1]);
    check_headers_scope(argv[1]);
    check_file_rules(argv[1]);
    check_jpx_rules(argv[1]);
    check_offsets(argv[1]);
    check_second_jp2h(argv[1]);
    check_codestream_lifetime(argv[1]);
    check_plt_next();
    check_link_paths();
    check_url_rule();
    check_long_url();
    check_jpx_name();
    check_rules();
    printf(failures ? "%d failures\n" : "all checks passed\n", failures);
    return failures != 0;
}
