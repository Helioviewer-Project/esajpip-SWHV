/* test_profile: the reader's served-profile mode against the corpus in
 * tests/vectors/j2k. The reader and the model's harness share the rules
 * (lib/hv_rules.c), so the reader's profile mode must accept a vector
 * exactly when the manifest labels it profile-valid: hv_check_jp2 or
 * hv_check_jpx, then every codestream with HV_PROFILE, embedded or in the
 * linked files (hv_check_link).
 *
 *   test_profile <vector directory> */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_reader.h"

static int failures;

static void check(int ok, const char *what, const char *detail) {
    if (!ok) {
        printf("FAIL %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
        failures++;
    }
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long n;
    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = malloc(n ? (size_t)n : 1)) != NULL && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        *size = (size_t)n;
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

/* NULL if the JP2 file passes hv_check_jp2 and its codestream reads
 * through with `flags`, otherwise the error. */
static const char *profile_error(const uint8_t *buf, size_t size, unsigned flags) {
    static char message[256];
    hv_box jp2c;
    size_t at;
    const char *error = hv_check_jp2(buf, size, &jp2c, &at);
    if (error == NULL)
        error = hv_codestream_check(buf, jp2c.payload, jp2c.end, flags, &at);
    if (error == NULL)
        return NULL;
    snprintf(message, sizeof message, "%s at %zu", error, at);
    return message;
}

/* The same for a JPX file at `path`: its codestreams, or its linked files,
 * which the server resolves from the JPX file's directory. */
static const char *jpx_profile_error(const char *path, const uint8_t *buf, size_t size) {
    static char message[4400];
    char linked[4096] = "";
    hv_jpx jpx;
    size_t at, i;
    const char *error = hv_check_jpx(buf, size, &jpx, &at);

    for (i = 0; error == NULL && i < jpx.count; i++) {
        if (jpx.jp2c != NULL) {
            error = hv_codestream_check(buf, jpx.jp2c[i].payload, jpx.jp2c[i].end, HV_PROFILE, &at);
        } else if (hv_link_path(&jpx.links[i], path, linked, sizeof linked) != 0) {
            error = "url: cannot decode the path";
            at = 0;
        } else {
            size_t link_size;
            uint8_t *link = read_file(linked, &link_size);
            at = 0;
            error = link ? hv_check_link(link, link_size, &jpx.links[i], &at)
                         : "url.missing-companion";
            free(link);
        }
    }
    hv_jpx_free(&jpx);
    if (error == NULL)
        return NULL;
    snprintf(message, sizeof message, "%s at %zu%s%s", error, at, linked[0] ? " of " : "",
             linked);
    return message;
}

static uint8_t *vector(const char *dir, const char *name, size_t *size) {
    char path[4096];
    uint8_t *buf;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if ((buf = read_file(path, size)) == NULL)
        check(0, "cannot read", path);
    return buf;
}

/* The corpus: every row of the manifest. */
static void check_corpus(const char *dir) {
    char path[4096], line[8192];
    FILE *manifest;
    int jp2 = 0, jpx = 0, valid = 0;

    snprintf(path, sizeof path, "%s/manifest.tsv", dir);
    if ((manifest = fopen(path, "r")) == NULL) {
        check(0, "cannot read", path);
        return;
    }
    check(fgets(line, sizeof line, manifest) != NULL &&
          strncmp(line, "file\tkind\tstandard\tprofile\treason\t", 34) == 0,
          "manifest header", NULL);
    while (fgets(line, sizeof line, manifest) != NULL) {
        char *field[5], *p = line, detail[8192];
        const char *error;
        uint8_t *buf;
        size_t size;
        int i, expected;

        for (i = 0; i < 5; i++) {
            field[i] = p;
            if ((p = strchr(p, '\t')) == NULL)
                break;
            *p++ = 0;
        }
        if (i < 5) {
            check(0, "manifest row", line);
            continue;
        }
        expected = strcmp(field[3], "valid") == 0;
        if ((buf = vector(dir, field[0], &size)) == NULL)
            continue;
        if (strcmp(field[1], "jpx") == 0) {
            snprintf(path, sizeof path, "%s/%s", dir, field[0]);
            error = jpx_profile_error(path, buf, size);
            jpx++;
        } else {
            error = profile_error(buf, size, HV_PROFILE);
            jp2++;
        }
        snprintf(detail, sizeof detail, "%s: reader %s, manifest %s (%s)", field[0],
                 error ? error : "valid", field[3], field[4]);
        check((error == NULL) == expected, "profile label", detail);
        valid += expected;
        free(buf);
    }
    fclose(manifest);
    printf("%d JP2 and %d JPX vectors, %d profile-valid\n", jp2, jpx, valid);
    check(jp2 > 0 && jpx > 0 && valid > 0, "corpus has JP2 and JPX vectors, some valid", NULL);
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
         "main header: marker outside MainMarkerCode-Profile"},
        {"jp2-main-ff30-length.jp2", "main header: marker outside MainMarkerCode-Profile"},
        {"jp2-main-ppt.jp2", "main header: marker outside MainMarkerCode-Profile"},
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

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: test_profile <vector directory>\n");
        return 2;
    }
    check_corpus(argv[1]);
    check_headers_scope(argv[1]);
    check_file_rules(argv[1]);
    printf(failures ? "%d failures\n" : "all checks passed\n", failures);
    return failures != 0;
}
