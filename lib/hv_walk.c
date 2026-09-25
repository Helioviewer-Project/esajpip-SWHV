/* hv_walk: checks the framing of JPEG 2000 files with hv_reader and prints
 * one line per file. With -v, prints every box and codestream item. With -p,
 * accepts trailing zero PLT entries (HV_ACCEPT_PLT_PADDING), as the server
 * does for deployed files.
 * Exit status: 0 if every file is valid, 1 otherwise, 2 on usage errors. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_reader.h"

static int verbose;
static unsigned flags;

static void fourcc(uint32_t t, char out[5]) {
    int i;
    for (i = 0; i < 4; i++) {
        char c = (char)(t >> (24 - 8 * i));
        out[i] = c >= 32 && c < 127 ? c : '.';
    }
    out[4] = 0;
}

typedef struct {
    int boxes, codestreams, tile_parts;
    unsigned long plt_padding;
    const char *error;
    size_t at;
} walk_result;

static int walk_codestream(const uint8_t *buf, size_t start, size_t end, walk_result *r) {
    hv_codestream cs;
    hv_item item;
    int status = hv_codestream_open(&cs, buf, start, end, flags);

    while (status == 0 && (status = hv_codestream_next(&cs, &item)) == 1) {
        if (item.kind == HV_TILE_PART)
            r->tile_parts++;
        if (item.kind == HV_TILE_DATA)
            r->plt_padding += item.plt_padding;
        if (verbose) {
            static const char *kinds[] = {"segment", "tile-part", "tile-segment", "data", "end"};
            printf("    %-12s %04X [%zu, %zu)\n", kinds[item.kind], item.code, item.start, item.end);
        }
        if (item.kind == HV_END)
            break;
        status = 0;
    }
    if (status < 0) {
        r->error = cs.error;
        r->at = cs.error_at;
    } else {
        r->codestreams++;
    }
    hv_codestream_close(&cs);
    return status < 0 ? -1 : 0;
}

static int walk_boxes(const uint8_t *buf, hv_boxes *it, int depth, walk_result *r) {
    hv_box box;
    int status;
    char name[5];

    while ((status = hv_boxes_next(it, &box, &r->error, &r->at)) == 1) {
        r->boxes++;
        if (verbose) {
            fourcc(box.type, name);
            printf("  %*s%s [%zu, %zu)%s\n", 2 * depth, "", name, box.start, box.end,
                   box.to_end ? " to end" : "");
        }
        if (box.type == 0x6A703263) {            /* jp2c */
            if (walk_codestream(buf, box.payload, box.end, r) != 0)
                return -1;
        } else if (hv_is_superbox(box.type)) {
            hv_boxes children;
            hv_boxes_children(&children, buf, &box);
            if (walk_boxes(buf, &children, depth + 1, r) != 0)
                return -1;
        }
    }
    return status < 0 ? -1 : 0;
}

static int walk_file(const char *path) {
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long size;
    walk_result r = {0, 0, 0, 0, NULL, 0};
    int status;

    if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0 || (buf = malloc(size ? (size_t)size : 1)) == NULL ||
        fread(buf, 1, (size_t)size, f) != (size_t)size) {
        printf("ERROR   %s: cannot read\n", path);
        if (f) fclose(f);
        free(buf);
        return -1;
    }
    fclose(f);
    if (size >= 2 && buf[0] == 0xFF && buf[1] == 0x4F) {
        status = walk_codestream(buf, 0, (size_t)size, &r);
    } else {
        hv_boxes it;
        hv_boxes_file(&it, buf, (size_t)size);
        status = walk_boxes(buf, &it, 0, &r);
    }
    if (status == 0 && r.plt_padding != 0)
        printf("valid   %s: %d boxes, %d codestreams, %d tile-parts, %lu zero PLT entries\n",
               path, r.boxes, r.codestreams, r.tile_parts, r.plt_padding);
    else if (status == 0)
        printf("valid   %s: %d boxes, %d codestreams, %d tile-parts\n", path, r.boxes,
               r.codestreams, r.tile_parts);
    else
        printf("invalid %s: %s at %zu\n", path, r.error, r.at);
    free(buf);
    return status;
}

int main(int argc, char **argv) {
    int i = 1, bad = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] && !argv[i][2]; i++) {
        if (argv[i][1] == 'v')
            verbose = 1;
        else if (argv[i][1] == 'p')
            flags |= HV_ACCEPT_PLT_PADDING;
        else
            break;
    }
    if (i == argc || argv[i][0] == '-') {
        fprintf(stderr, "usage: hv_walk [-v] [-p] file...\n");
        return 2;
    }
    for (; i < argc; i++)
        bad |= walk_file(argv[i]) != 0;
    return bad;
}
