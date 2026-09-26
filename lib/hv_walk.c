/* hv_walk: checks JPEG 2000 files with hv_reader and prints one line per
 * file: "valid", "invalid" with the error and its offset, "differs" when
 * the file is valid but -w rewrote it differently, or "ERROR" when it
 * cannot be read. Exit status: 0 if every file is valid (and, with -w,
 * rewritten identically), 1 otherwise, 2 on usage errors.
 *
 *   -v  print every box and codestream item
 *   -p  accept trailing zero PLT entries of each tile-part
 *       (HV_ACCEPT_PLT_PADDING), as deployed files carry them; not with -P,
 *       whose profile accepts them after the last packet only
 *   -P  check the served profile (hv_check_served): the file rules, and
 *       HV_PROFILE for every codestream, embedded or in a linked file
 *   -H  check the header boxes (hv_check_jp2h, or hv_check_jpx_headers), as
 *       the tools do for the files they write
 *   -w  rewrite the whole file with hv_writer from what the reader decoded,
 *       and compare it with the input
 *
 * A file whose name ends in .jpx, in any case, is a JPX file, as the
 * server decides; any other is a JP2 file, or, if it starts with SOC, a
 * raw codestream. Without -P and -H a raw codestream is read as such; -P
 * and -H check files, and fail it with file.signature. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_reader.h"
#include "hv_served.h"
#include "hv_writer.h"

/* A marker code, and the marker and length that start a marker segment
 * (T.800 A.1). */
#define MARKER HV_FIXED(MarkerCode)
#define SEGMENT_START (HV_FIXED(MarkerCode) + HV_FIXED(SegmentLength))

static int verbose, rewrite, profile, headers;
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
    char message[64];       /* error, when it is formatted here */
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
        if (hv_write_marker(out, HV_SOD) != 0 ||
            hv_write_bytes(out, buf + item->start, item->end - item->start) != 0)
            return -1;
        return hv_end_tile_part(out, *tp_start);
    case HV_END:
        return hv_write_marker(out, HV_EOC);
    case HV_SEGMENT:
    case HV_TILE_SEGMENT:
        if (item->siz)
            return hv_write_siz(out, item->siz);
        if (item->cod)
            return hv_write_cod(out, item->cod);
        if (item->qcd)
            return hv_write_qcd(out, item->qcd);
        if (item->com)
            return hv_write_com(out, item->com->rcom, item->com->text, item->com->size);
        if (item->plt) {
            /* The reader has checked every entry: hv_plt_next ends with 0. */
            size_t pos = item->plt->start;
            uint64_t v;
            int more;
            while ((more = hv_plt_next(buf, &pos, item->plt->end, &v)) == 1) {
                if (v == 0)
                    r->uncomparable = "zero PLT entries are dropped";
                else if (add_length(r, v) != 0)
                    return -1;
            }
            return more;
        }
        if (item->end - item->start == MARKER)
            return hv_write_marker(out, item->code);
        return hv_write_segment(out, item->code, buf + item->start + SEGMENT_START,
                                item->end - item->start - SEGMENT_START);
    }
    return -1;
}

static int walk_codestream(const uint8_t *buf, size_t start, size_t end, walk_result *r) {
    hv_codestream cs;
    hv_item item;
    size_t tp_start = 0, at = start;     /* at: the item being rewritten */
    int status = hv_codestream_open(&cs, buf, start, end, flags);

    if (status == 0 && rewrite && hv_write_marker(&r->out, HV_SOC) != 0)
        status = -2;
    while (status == 0 && (status = hv_codestream_next(&cs, &item)) == 1) {
        static const char *kinds[] = {"segment", "tile-part", "tile-segment", "data", "end"};
        at = item.start;
        if (item.kind == HV_TILE_PART)
            r->tile_parts++;
        if (item.kind == HV_TILE_DATA)
            r->plt_padding += item.plt_padding;
        if (verbose)
            printf("    %-12s %04X [%zu, %zu)\n", kinds[item.kind], item.code, item.start,
                   item.end);
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
        r->at = at;
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
            if (hv_begin_box(&r->out, box.type, box.payload - box.start == HV_BOX_HEADER_XL,
                             &start) != 0)
                goto write_failed;
        }
        if (box.type == HV_BOX_JP2C) {
            if (walk_codestream(buf, box.payload, box.end, r) != 0)
                return -1;
        } else if (hv_is_superbox(box.type)) {
            hv_boxes children;
            if (depth + 1 > HV_BOX_DEPTH_MAX) {       /* depth 0 is the top level */
                snprintf(r->message, sizeof r->message, "superboxes nested deeper than %d",
                         HV_BOX_DEPTH_MAX);
                r->error = r->message;
                r->at = box.start;
                return -1;
            }
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

/* Nonzero when the file starts with SOC: a raw codestream. */
static int raw_codestream(const uint8_t *buf, size_t size) {
    return size >= MARKER && buf[0] == HV_SOC >> 8 && buf[1] == (HV_SOC & 0xFF);
}

static int walk_file(const char *path) {
    uint8_t *buf;
    size_t size = 0, linked = 0, i, n;
    walk_result r;
    hv_served served;
    int status, jpx = hv_is_jpx_name(path), raw;

    memset(&r, 0, sizeof r);
    served.linked_path[0] = 0;
    served.at_linked = 0;
    hv_out_init(&r.out);
    if ((buf = hv_load_file(path, &size)) == NULL) {
        printf("ERROR   %s: cannot read\n", path);
        return -1;
    }
    raw = raw_codestream(buf, size);
    if (profile) {
        r.error = hv_check_served(path, buf, size, jpx, &served);
        r.at = served.at;
        linked = served.linked;
    }
    if (headers && r.error == NULL) {
        if (raw) {
            r.error = "file.signature";
            r.at = 0;
        } else {
            r.error = jpx ? hv_check_jpx_headers(buf, size, &r.at)
                          : hv_check_jp2h(buf, size, &r.at);
        }
    }
    if (r.error != NULL) {
        status = -1;
    } else if (raw) {
        status = walk_codestream(buf, 0, size, &r);
    } else {
        hv_boxes it;
        hv_boxes_file(&it, buf, size);
        status = walk_boxes(buf, &it, 0, &r);
    }
    if (status != 0) {
        if (served.linked_path[0] == 0)
            printf("invalid %s: %s at %zu\n", path, r.error, r.at);
        else if (served.at_linked)
            printf("invalid %s: %s at %zu of %s\n", path, r.error, r.at, served.linked_path);
        else
            printf("invalid %s: %s at %zu (linked file %s)\n", path, r.error, r.at,
                   served.linked_path);
    } else if (rewrite && r.uncomparable == NULL &&
               (r.out.size != size || (size != 0 && memcmp(r.out.data, buf, size) != 0))) {
        n = r.out.size < size ? r.out.size : size;
        for (i = 0; i < n && r.out.data[i] == buf[i]; i++)
            ;
        printf("differs %s: valid, but the rewrite differs at %zu\n", path, i);
        status = -1;
    } else {
        printf("valid   %s: %d boxes, %d codestreams, %d tile-parts", path, r.boxes,
               r.codestreams, r.tile_parts);
        if (linked != 0)
            printf(", %zu linked codestreams", linked);
        if (r.plt_padding != 0)
            printf(", %lu zero PLT entries", r.plt_padding);
        if (rewrite && r.uncomparable != NULL)
            printf("; rewrite not compared: %s", r.uncomparable);
        else if (rewrite)
            printf("; rewritten identically");
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
        else if (argv[i][1] == 'P') {
            flags |= HV_PROFILE;
            profile = 1;
        }
        else if (argv[i][1] == 'H')
            headers = 1;
        else if (argv[i][1] == 'w')
            rewrite = 1;
        else
            break;
    }
    if (i == argc || argv[i][0] == '-' || ((flags & HV_ACCEPT_PLT_PADDING) && profile)) {
        fprintf(stderr, "usage: hv_walk [-v] [-p] [-P] [-H] [-w] file...\n");
        return 2;
    }
    for (; i < argc; i++)
        bad |= walk_file(argv[i]) != 0;
    return bad;
}
