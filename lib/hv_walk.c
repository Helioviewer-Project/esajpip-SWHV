/* hv_walk: checks JPEG 2000 files with hv_reader and prints one line per
 * file. Exit status: 0 if every file is valid (and, with -w, rewritten
 * identically), 1 otherwise, 2 on usage errors.
 *
 *   -v  print every box and codestream item
 *   -p  accept trailing zero PLT entries (HV_ACCEPT_PLT_PADDING), as the
 *       server does for deployed files
 *   -w  rewrite the whole file with hv_writer from what the reader decoded,
 *       and compare it with the input */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_reader.h"
#include "hv_writer.h"

static int verbose, rewrite;
static unsigned flags;

enum { JP2C = 0x6A703263, SOC = 0xFF4F, SOD = 0xFF93, EOC = 0xFFD9 };

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
    hv_out out;             /* -w: the rewritten file */
    const char *uncomparable;   /* -w: why the rewrite cannot match the input */
    uint64_t *lengths;      /* -w: PLT entries of the current tile-part */
    size_t nlengths, maxlengths;
} walk_result;

static int add_length(walk_result *r, uint64_t v) {
    if (r->nlengths == r->maxlengths) {
        size_t n = r->maxlengths ? 2 * r->maxlengths : 1024;
        uint64_t *p = realloc(r->lengths, n * sizeof *p);
        if (p == NULL)
            return -1;
        r->lengths = p;
        r->maxlengths = n;
    }
    r->lengths[r->nlengths++] = v;
    return 0;
}

static int flush_plt(walk_result *r) {
    int status = r->nlengths ? hv_write_plt(&r->out, r->lengths, r->nlengths) : 0;
    r->nlengths = 0;
    return status;
}

/* Writes one codestream item from its decoded form. */
static int rewrite_item(const uint8_t *buf, const hv_item *item, walk_result *r,
                        size_t *tp_start) {
    hv_out *out = &r->out;
    int i;

    if (item->kind != HV_TILE_SEGMENT || item->plt == NULL)
        if (flush_plt(r) != 0)
            return -1;
    switch (item->kind) {
    case HV_TILE_PART:
        if (item->sot.psot == 0)
            r->uncomparable = "Psot = 0 is written as an explicit length";
        return hv_begin_tile_part(out, (uint16_t)item->sot.isot, (uint8_t)item->sot.tpsot,
                                  (uint8_t)item->sot.tnsot, tp_start);
    case HV_TILE_DATA:
        if (hv_write_marker(out, SOD) != 0 ||
            hv_write_bytes(out, buf + item->start, item->end - item->start) != 0)
            return -1;
        return hv_end_tile_part(out, *tp_start);
    case HV_END:
        return hv_write_marker(out, EOC);
    case HV_SEGMENT:
    case HV_TILE_SEGMENT:
        if (item->siz)
            return hv_write_siz(out, item->siz);
        if (item->cod)
            return hv_write_cod(out, item->cod);
        if (item->qcd)
            return hv_write_qcd(out, item->qcd);
        if (item->com)
            return hv_write_com(out, item->com);
        if (item->plt) {
            for (i = 0; i < item->plt->body.entries.nCount; i++) {
                uint64_t v = hv_iplt_value(&item->plt->body.entries.arr[i]);
                if (v == 0)
                    r->uncomparable = "zero PLT entries are dropped";
                else if (add_length(r, v) != 0)
                    return -1;
            }
            return 0;
        }
        if (item->end - item->start == 2)
            return hv_write_marker(out, item->code);
        return hv_write_segment(out, item->code, buf + item->start + 4,
                                item->end - item->start - 4);
    }
    return -1;
}

static int walk_codestream(const uint8_t *buf, size_t start, size_t end, walk_result *r) {
    hv_codestream cs;
    hv_item item;
    size_t tp_start = 0;
    int status = hv_codestream_open(&cs, buf, start, end, flags);

    if (status == 0 && rewrite && hv_write_marker(&r->out, SOC) != 0)
        status = -2;
    while (status == 0 && (status = hv_codestream_next(&cs, &item)) == 1) {
        if (item.kind == HV_TILE_PART)
            r->tile_parts++;
        if (item.kind == HV_TILE_DATA)
            r->plt_padding += item.plt_padding;
        if (verbose) {
            static const char *kinds[] = {"segment", "tile-part", "tile-segment", "data", "end"};
            printf("    %-12s %04X [%zu, %zu)\n", kinds[item.kind], item.code, item.start, item.end);
        }
        if (rewrite && rewrite_item(buf, &item, r, &tp_start) != 0) {
            status = -2;
            break;
        }
        if (item.kind == HV_END)
            break;
        status = 0;
    }
    if (status == -1) {
        r->error = cs.error;
        r->at = cs.error_at;
    } else if (status == -2) {
        r->error = r->out.error ? r->out.error : "out of memory";
        r->at = item.start;
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
        size_t start = 0;
        r->boxes++;
        if (verbose) {
            fourcc(box.type, name);
            printf("  %*s%s [%zu, %zu)%s\n", 2 * depth, "", name, box.start, box.end,
                   box.to_end ? " to end" : "");
        }
        if (rewrite) {
            if (box.to_end)
                r->uncomparable = "LBox = 0 is written as an explicit length";
            if (hv_begin_box(&r->out, box.type, box.payload - box.start == 16, &start) != 0)
                goto write_failed;
        }
        if (box.type == JP2C) {
            if (walk_codestream(buf, box.payload, box.end, r) != 0)
                return -1;
        } else if (hv_is_superbox(box.type)) {
            hv_boxes children;
            hv_boxes_children(&children, buf, &box);
            if (walk_boxes(buf, &children, depth + 1, r) != 0)
                return -1;
        } else if (rewrite &&
                   hv_write_bytes(&r->out, buf + box.payload, box.end - box.payload) != 0) {
            goto write_failed;
        }
        if (rewrite && hv_end_box(&r->out, start) != 0)
            goto write_failed;
    }
    return status < 0 ? -1 : 0;

write_failed:
    r->error = r->out.error;
    r->at = box.start;
    return -1;
}

static int walk_file(const char *path) {
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long size;
    walk_result r;
    int status;

    memset(&r, 0, sizeof r);
    hv_out_init(&r.out);
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
    if (status != 0) {
        printf("invalid %s: %s at %zu\n", path, r.error, r.at);
    } else {
        printf("valid   %s: %d boxes, %d codestreams, %d tile-parts", path, r.boxes,
               r.codestreams, r.tile_parts);
        if (r.plt_padding != 0)
            printf(", %lu zero PLT entries", r.plt_padding);
        if (rewrite) {
            size_t i = 0, n = r.out.size < (size_t)size ? r.out.size : (size_t)size;
            while (i < n && r.out.data[i] == buf[i])
                i++;
            if (r.uncomparable != NULL)
                printf("; rewrite not compared: %s", r.uncomparable);
            else if (i == n && r.out.size == (size_t)size)
                printf("; rewritten identically");
            else {
                printf("; rewrite differs at %zu", i);
                status = -1;
            }
        }
        printf("\n");
    }
    hv_out_free(&r.out);
    free(r.lengths);
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
        else if (argv[i][1] == 'w')
            rewrite = 1;
        else
            break;
    }
    if (i == argc || argv[i][0] == '-') {
        fprintf(stderr, "usage: hv_walk [-v] [-p] [-w] file...\n");
        return 2;
    }
    for (; i < argc; i++)
        bad |= walk_file(argv[i]) != 0;
    return bad;
}
