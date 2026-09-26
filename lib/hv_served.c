/* hv_served.c: see hv_served.h. */
#include "hv_served.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *hv_load_file(const char *path, size_t *size) {
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

int hv_is_jpx_name(const char *path) {
    static const char suffix[] = ".jpx";
    size_t n = strlen(path), k = sizeof suffix - 1, i;
    if (n < k)
        return 0;
    for (i = 0; i < k; i++)
        if (tolower((unsigned char)path[n - k + i]) != suffix[i])
            return 0;
    return 1;
}

/* One linked codestream: its file, found and read as the server does. */
static const char *check_linked(const char *path, const uint8_t *buf, const hv_link *link,
                                hv_served *r) {
    const char *error;
    uint8_t *linked;
    size_t size = 0;

    r->at = (size_t)(link->loc - buf);
    if ((error = hv_link_path(link, path, r->linked_path, sizeof r->linked_path)) != NULL)
        return error;
    if ((linked = hv_load_file(r->linked_path, &size)) == NULL)
        return "url.missing-companion";
    error = hv_check_link(linked, size, link, &r->at);
    r->at_linked = error != NULL;
    free(linked);
    return error;
}

const char *hv_check_served(const char *path, const uint8_t *buf, size_t size, int jpx,
                            hv_served *r) {
    const char *error;
    hv_jpx file;
    hv_box jp2c;
    size_t i;

    r->embedded = r->linked = 0;
    r->at = 0;
    r->at_linked = 0;
    r->linked_path[0] = 0;
    if (!jpx) {
        if ((error = hv_check_jp2(buf, size, &jp2c, &r->at)) == NULL &&
            (error = hv_codestream_check(buf, jp2c.payload, jp2c.end, HV_PROFILE, &r->at)) ==
                NULL)
            r->embedded = 1;
        return error;
    }
    if ((error = hv_check_jpx(buf, size, &file, &r->at)) != NULL)
        return error;
    for (i = 0; i < file.count && error == NULL; i++) {
        if (file.jp2c != NULL) {
            error = hv_codestream_check(buf, file.jp2c[i].payload, file.jp2c[i].end, HV_PROFILE,
                                        &r->at);
            r->embedded += error == NULL;
        } else {
            error = check_linked(path, buf, &file.links[i], r);
            r->linked += error == NULL;
        }
    }
    if (error == NULL)
        r->linked_path[0] = 0;
    hv_jpx_free(&file);
    return error;
}
