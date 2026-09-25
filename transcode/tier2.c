/* tier2.c: see tier2.h. The structure follows hvJP2K's jp2_packets.pyx
 * function by function; errors that Cython raised as exceptions from deep
 * inside the bit reader longjmp back to the entry point instead. */
#include "tier2.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INF ((int32_t)1 << 30)
#define HUGE_LENGTH ((int64_t)1 << 62)

typedef struct {
    jmp_buf jump;
    char *error;
    size_t error_size;
} failure;

static void fail(failure *f, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(f->error, f->error_size, format, args);
    va_end(args);
    longjmp(f->jump, 1);
}

static void *allocate(failure *f, size_t count, size_t size) {
    void *p;
    if (count != 0 && size > SIZE_MAX / count)
        fail(f, "out of memory");
    p = malloc(count ? count * size : 1);
    if (p == NULL)
        fail(f, "out of memory");
    return p;
}

/* ------------------------------------------------------------------------
 * Bit I/O with packet-header bit stuffing
 * ------------------------------------------------------------------------ */

typedef struct {
    const uint8_t *data;
    size_t size, pos;
    int buf, ct;
    failure *f;
} reader;

static int read_bit(reader *rd) {
    if (rd->ct == 0) {
        /* After 0xFF the next byte carries only 7 bits. */
        rd->ct = rd->buf == 0xFF ? 7 : 8;
        if (rd->pos >= rd->size)
            fail(rd->f, "truncated input is not supported: incomplete packet header");
        rd->buf = rd->data[rd->pos++];
    }
    rd->ct--;
    return (rd->buf >> rd->ct) & 1;
}

/* Saturates instead of overflowing: a value this large cannot fit in the
 * tile, and the caller then reports the overrun. */
static int64_t read_bits(reader *rd, int64_t n) {
    int64_t value = 0;
    while (n > 0) {
        int b = read_bit(rd);
        if (value < HUGE_LENGTH) {
            value = value * 2 + b;
            if (value > HUGE_LENGTH)
                value = HUGE_LENGTH;
        }
        n--;
    }
    return value;
}

/* End of a packet header: skip the stuffed byte after a final 0xFF. */
static size_t read_align(reader *rd) {
    if (rd->buf == 0xFF)
        rd->pos++;
    rd->ct = 0;
    rd->buf = 0;
    return rd->pos;
}

typedef struct {
    uint8_t *out;
    size_t size, capacity;
    int cur, n, cap;
    failure *f;
} writer;

static void reserve(writer *wr, size_t extra) {
    size_t need, capacity;
    uint8_t *grown;
    if (extra > SIZE_MAX - wr->size)
        fail(wr->f, "out of memory");
    need = wr->size + extra;
    if (need <= wr->capacity)
        return;
    capacity = wr->capacity ? wr->capacity : 4096;
    while (capacity < need)
        capacity = capacity > SIZE_MAX / 2 ? need : capacity * 2;
    if ((grown = realloc(wr->out, capacity)) == NULL)
        fail(wr->f, "out of memory");
    wr->out = grown;
    wr->capacity = capacity;
}

static void emit(writer *wr) {
    reserve(wr, 1);
    wr->out[wr->size++] = (uint8_t)wr->cur;
    wr->cap = wr->cur == 0xFF ? 7 : 8;
    wr->cur = 0;
    wr->n = 0;
}

static void put_bit(writer *wr, int b) {
    wr->cur = (wr->cur << 1) | b;
    if (++wr->n == wr->cap)
        emit(wr);
}

static void put_bits(writer *wr, uint64_t v, int n) {
    while (n > 0) {
        n--;
        put_bit(wr, (int)((v >> n) & 1));
    }
}

static void flush_header(writer *wr) {
    if (wr->n) {
        reserve(wr, 1);
        wr->out[wr->size++] = (uint8_t)(wr->cur << (wr->cap - wr->n));
    }
    /* A packet header must not end with 0xFF. */
    if (wr->size && wr->out[wr->size - 1] == 0xFF) {
        reserve(wr, 1);
        wr->out[wr->size++] = 0;
    }
    wr->cur = 0;
    wr->n = 0;
    wr->cap = 8;
}

/* ------------------------------------------------------------------------
 * Tag trees (B.10.2), all of them in shared flat arrays
 * ------------------------------------------------------------------------ */

typedef struct {
    int32_t *parent, *value, *low;
    uint8_t *known;
} trees;

static size_t tree_size(int w, int h) {
    size_t total = 0;
    for (;;) {
        total += (size_t)w * h;
        if ((size_t)w * h == 1)
            return total;
        w = (w + 1) >> 1;
        h = (h + 1) >> 1;
    }
}

/* Node indices are absolute; each level follows the previous one. */
static void tree_init(trees *t, size_t base, int w, int h) {
    size_t level = base, next, k;
    int x, y, pw;
    for (;;) {
        next = level + (size_t)w * h;
        if ((size_t)w * h == 1) {
            t->parent[level] = -1;
            break;
        }
        pw = (w + 1) >> 1;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                t->parent[level + (size_t)y * w + x] =
                        (int32_t)(next + (size_t)(y >> 1) * pw + (x >> 1));
        level = next;
        w = pw;
        h = (h + 1) >> 1;
    }
    for (k = base; k < next; k++) {
        t->value[k] = INF;
        t->low[k] = 0;
        t->known[k] = 0;
    }
}

/* Parents have larger indices, so one forward pass sets every minimum. */
static void tree_propagate(trees *t, size_t base, size_t size) {
    size_t k;
    for (k = base; k < base + size; k++) {
        int32_t p = t->parent[k];
        if (p >= 0 && t->value[k] < t->value[p])
            t->value[p] = t->value[k];
    }
}

static int tree_path(const trees *t, size_t leaf, int32_t *stack) {
    int depth = 0;
    int64_t n = (int64_t)leaf;
    while (n >= 0) {
        stack[depth++] = (int32_t)n;
        n = t->parent[n];
    }
    return depth;
}

static int tree_decode(trees *t, reader *rd, size_t leaf, int32_t threshold) {
    int32_t stack[64], low = 0;
    int depth = tree_path(t, leaf, stack), i;
    for (i = depth - 1; i >= 0; i--) {
        int32_t n = stack[i];
        if (low > t->low[n])
            t->low[n] = low;
        else
            low = t->low[n];
        while (low < threshold && low < t->value[n]) {
            if (read_bit(rd))
                t->value[n] = low;
            else
                low++;
        }
        t->low[n] = low;
    }
    return t->value[leaf] < threshold;
}

static void tree_encode(trees *t, writer *wr, size_t leaf, int32_t threshold) {
    int32_t stack[64], low = 0;
    int depth = tree_path(t, leaf, stack), i;
    for (i = depth - 1; i >= 0; i--) {
        int32_t n = stack[i];
        if (low > t->low[n])
            t->low[n] = low;
        else
            low = t->low[n];
        while (low < threshold) {
            if (low >= t->value[n]) {
                if (!t->known[n]) {
                    put_bit(wr, 1);
                    t->known[n] = 1;
                }
                break;
            }
            put_bit(wr, 0);
            low++;
        }
        t->low[n] = low;
    }
}

static void trees_alloc(failure *f, trees *t, size_t size) {
    t->parent = allocate(f, size, sizeof *t->parent);
    t->value = allocate(f, size, sizeof *t->value);
    t->low = allocate(f, size, sizeof *t->low);
    t->known = allocate(f, size, 1);
}

static void trees_free(trees *t) {
    free(t->parent);
    free(t->value);
    free(t->low);
    free(t->known);
    memset(t, 0, sizeof *t);
}

/* ------------------------------------------------------------------------
 * Coding-pass count code words (Table B.4)
 * ------------------------------------------------------------------------ */

static int read_npasses(reader *rd) {
    int64_t n;
    if (!read_bit(rd))
        return 1;
    if (!read_bit(rd))
        return 2;
    if ((n = read_bits(rd, 2)) != 3)
        return (int)(3 + n);
    if ((n = read_bits(rd, 5)) != 31)
        return (int)(6 + n);
    return (int)(37 + read_bits(rd, 7));
}

static void write_npasses(writer *wr, int n) {
    if (n == 1)
        put_bit(wr, 0);
    else if (n == 2)
        put_bits(wr, 2, 2);
    else if (n <= 5)
        put_bits(wr, 12 | (n - 3), 4);
    else if (n <= 36)
        put_bits(wr, 0x1E0 | (n - 6), 9);
    else if (n <= 164)
        put_bits(wr, ((uint64_t)0x1FF << 7) | (n - 37), 16);
    else
        fail(wr->f, "too many coding passes: %d", n);
}

static int floorlog2(uint64_t n) {
    int r = -1;
    while (n) {
        n >>= 1;
        r++;
    }
    return r;
}

/* Tag-tree bases, one per (precinct, band) at precinct * 3 + band, in
 * precinct number order; bands without code-blocks get none. */
#define NO_TREE SIZE_MAX

static size_t tree_bases(const hv_geometry *g, size_t *base) {
    size_t nres = (size_t)g->ncomps * (g->levels + 1), ri, total = 0;
    uint64_t i, n;
    int b;
    for (ri = 0; ri < nres; ri++) {
        const hv_resolution *res = &g->res[ri];
        n = (uint64_t)(res->pw * res->ph);
        for (i = 0; i < n; i++) {
            size_t *slot = &base[(res->first_precinct + i) * 3];
            int64_t px, py;
            hv_precinct_cell(res, i, &px, &py);
            for (b = 0; b < 3; b++) {
                hv_block_rect rect = b < res->nbands ? hv_precinct_blocks(res, b, px, py)
                                                     : (hv_block_rect){0, 0, 0, 0};
                slot[b] = NO_TREE;
                if (rect.w * rect.h > 0) {
                    slot[b] = total;
                    total += tree_size((int)rect.w, (int)rect.h);
                }
            }
        }
    }
    return total;
}

/* A packet's precinct: its resolution, number and grid cell. */
typedef struct {
    const hv_resolution *res;
    uint64_t number;
    int64_t px, py;
} precinct_ref;

static precinct_ref packet_precinct(const hv_geometry *g, const hv_packet *pk) {
    precinct_ref p;
    p.res = &g->res[pk->resolution];
    p.number = p.res->first_precinct + pk->precinct;
    hv_precinct_cell(p.res, pk->precinct, &p.px, &p.py);
    return p;
}

/* ------------------------------------------------------------------------
 * Packets
 * ------------------------------------------------------------------------ */

/* Everything a packet pass allocates, on the heap: after a longjmp, only
 * locals left unchanged since setjmp are reliable, and the pointer to this
 * context is one. */
typedef struct {
    failure f;
    size_t *base;
    int64_t *lblock;
    int32_t *contrib;
    trees incl, zbp;
    writer wr;
} context;

static context *context_new(char *error, size_t error_size) {
    context *c = calloc(1, sizeof *c);
    if (c == NULL) {
        snprintf(error, error_size, "out of memory");
        return NULL;
    }
    c->f.error = error;
    c->f.error_size = error_size;
    c->wr.cap = 8;
    c->wr.f = &c->f;
    return c;
}

static void context_free(context *c) {
    free(c->base);
    free(c->lblock);
    free(c->contrib);
    trees_free(&c->incl);
    trees_free(&c->zbp);
    free(c->wr.out);
    free(c);
}

/* Allocates the tag trees of every (precinct, band) with code-blocks and
 * sets them up, with leaf values from `bl` when writing (the encoder knows
 * every value in advance) or unknown when reading. */
static void trees_setup(context *c, const hv_geometry *g, const hv_blocks *bl, int write) {
    size_t nres = (size_t)g->ncomps * (g->levels + 1), ri, nodes;
    uint64_t i, n;
    int b;

    c->base = allocate(&c->f, (size_t)g->nprecincts * 3, sizeof *c->base);
    nodes = tree_bases(g, c->base);
    trees_alloc(&c->f, &c->incl, nodes);
    trees_alloc(&c->f, &c->zbp, nodes);
    for (ri = 0; ri < nres; ri++) {
        const hv_resolution *res = &g->res[ri];
        n = (uint64_t)(res->pw * res->ph);
        for (i = 0; i < n; i++) {
            const size_t *slot = &c->base[(res->first_precinct + i) * 3];
            int64_t px, py, cx, cy;
            hv_precinct_cell(res, i, &px, &py);
            for (b = 0; b < res->nbands; b++) {
                hv_block_rect rect = hv_precinct_blocks(res, b, px, py);
                size_t leaf = slot[b];
                if (leaf == NO_TREE)
                    continue;
                tree_init(&c->incl, leaf, (int)rect.w, (int)rect.h);
                tree_init(&c->zbp, leaf, (int)rect.w, (int)rect.h);
                if (!write)
                    continue;
                for (cy = rect.cy0; cy < rect.cy0 + rect.h; cy++)
                    for (cx = rect.cx0; cx < rect.cx0 + rect.w; cx++, leaf++) {
                        size_t blk = (size_t)hv_block_number(&res->band[b], cx, cy);
                        c->incl.value[leaf] = bl->incl[blk] >= 0 ? bl->incl[blk] : bl->nlayers;
                        c->zbp.value[leaf] = bl->zbp[blk] >= 0 ? bl->zbp[blk] : 0;
                    }
                tree_propagate(&c->incl, slot[b], tree_size((int)rect.w, (int)rect.h));
                tree_propagate(&c->zbp, slot[b], tree_size((int)rect.w, (int)rect.h));
            }
        }
    }
}

int hv_read_packets(const hv_geometry *g, const hv_packet *packets, size_t npackets,
                    const uint8_t *data, size_t size, const int64_t *tile_part_ends,
                    size_t nparts, int zero_psot, int sop, int eph, hv_blocks *bl,
                    char *error, size_t error_size) {
    context *const c = context_new(error, error_size);
    reader rd;
    size_t pkt, part = 0, pos = 0, k;
    int nlayers = bl->nlayers;

    if (c == NULL)
        return -1;
    if (setjmp(c->f.jump)) {
        context_free(c);
        return -1;
    }
    rd.data = data;
    rd.size = size;
    rd.f = &c->f;

    c->lblock = allocate(&c->f, bl->nblocks, sizeof *c->lblock);
    c->contrib = allocate(&c->f, bl->nblocks, sizeof *c->contrib);
    trees_setup(c, g, bl, 0);
    for (k = 0; k < bl->nblocks; k++)
        c->lblock[k] = 3;

    for (pkt = 0; pkt < npackets; pkt++) {
        precinct_ref p = packet_precinct(g, &packets[pkt]);
        int32_t l = packets[pkt].layer;
        int ncontrib = 0, i, b;
        /* For Psot = 0, another SOT can only be identified at a packet
         * boundary. */
        if (zero_psot && pos + 1 < size && data[pos] == 0xFF && data[pos + 1] == 0x90)
            fail(&c->f, "Psot=0 is only valid for the last tile-part");
        /* Empty tile-parts share an end offset with the preceding part. */
        while (part < nparts && (int64_t)pos == tile_part_ends[part])
            part++;
        if (part == nparts)
            fail(&c->f, "truncated input is not supported: missing packets");
        if (sop && pos + 1 < size && data[pos] == 0xFF && data[pos + 1] == 0x91)
            pos += 6;

        rd.pos = pos;
        rd.buf = 0;
        rd.ct = 0;
        if (read_bit(&rd)) {
            for (b = 0; b < p.res->nbands; b++) {
                hv_block_rect rect = hv_precinct_blocks(p.res, b, p.px, p.py);
                size_t leaf = c->base[p.number * 3 + b];
                int64_t cx, cy;
                if (leaf == NO_TREE)
                    continue;
                for (cy = rect.cy0; cy < rect.cy0 + rect.h; cy++)
                    for (cx = rect.cx0; cx < rect.cx0 + rect.w; cx++, leaf++) {
                        size_t blk = (size_t)hv_block_number(&p.res->band[b], cx, cy), at;
                        int n;
                        int64_t len_bits;
                        if (bl->incl[blk] < 0) {
                            if (!tree_decode(&c->incl, &rd, leaf, l + 1))
                                continue;
                            tree_decode(&c->zbp, &rd, leaf, INF);
                            bl->zbp[blk] = c->zbp.value[leaf];
                            bl->incl[blk] = l;
                        } else if (!read_bit(&rd)) {
                            continue;
                        }
                        n = read_npasses(&rd);
                        while (read_bit(&rd))
                            c->lblock[blk]++;
                        len_bits = c->lblock[blk] + floorlog2((uint64_t)n);
                        at = blk * nlayers + l;
                        bl->npasses[at] = n;
                        bl->length[at] = read_bits(&rd, len_bits);
                        c->contrib[ncontrib++] = (int32_t)blk;
                    }
            }
        }
        pos = read_align(&rd);
        if (eph && pos + 1 < size && data[pos] == 0xFF && data[pos + 1] == 0x92)
            pos += 2;
        for (i = 0; i < ncontrib; i++) {
            size_t at = (size_t)c->contrib[i] * nlayers + l;
            int64_t seg = bl->length[at];
            bl->offset[at] = (int64_t)pos;
            if (seg >= HUGE_LENGTH || (uint64_t)seg > size - (pos < size ? pos : size))
                pos = size + 1;
            else
                pos += (size_t)seg;
        }
        if (pos > size)
            fail(&c->f, "truncated input is not supported: packet data overruns the tile");
        if ((int64_t)pos > tile_part_ends[part])
            fail(&c->f, "packet crosses a tile-part boundary");
    }
    if (pos != size) {
        if (zero_psot && pos + 1 < size && data[pos] == 0xFF && data[pos + 1] == 0x90)
            fail(&c->f, "Psot=0 is only valid for the last tile-part");
        fail(&c->f, "%zu unparsed tile bytes", size - pos);
    }
    context_free(c);
    return 0;
}

int hv_write_packets(const hv_geometry *g, const hv_packet *packets, size_t npackets,
                     const uint8_t *data, const hv_blocks *bl, uint8_t **out,
                     size_t *out_size, uint64_t *packet_lengths, char *error,
                     size_t error_size) {
    context *const c = context_new(error, error_size);
    size_t pkt, k;
    int nlayers = bl->nlayers;

    if (c == NULL)
        return -1;
    if (setjmp(c->f.jump)) {
        context_free(c);
        return -1;
    }

    c->lblock = allocate(&c->f, bl->nblocks, sizeof *c->lblock);
    c->contrib = allocate(&c->f, bl->nblocks, sizeof *c->contrib);
    trees_setup(c, g, bl, 1);
    for (k = 0; k < bl->nblocks; k++)
        c->lblock[k] = 3;

    for (pkt = 0; pkt < npackets; pkt++) {
        precinct_ref p = packet_precinct(g, &packets[pkt]);
        int32_t l = packets[pkt].layer;
        size_t start = c->wr.size;
        int ncontrib = 0, i, b;
        /* Like Kakadu, never use the one-bit empty packet; always write the
         * inclusion bits, even when no code-block contributes. */
        put_bit(&c->wr, 1);
        for (b = 0; b < p.res->nbands; b++) {
            hv_block_rect rect = hv_precinct_blocks(p.res, b, p.px, p.py);
            size_t leaf = c->base[p.number * 3 + b];
            int64_t cx, cy;
            if (leaf == NO_TREE)
                continue;
            for (cy = rect.cy0; cy < rect.cy0 + rect.h; cy++)
                for (cx = rect.cx0; cx < rect.cx0 + rect.w; cx++, leaf++) {
                    size_t blk = (size_t)hv_block_number(&p.res->band[b], cx, cy);
                    size_t at = blk * nlayers + l;
                    int n, log_n, need, inc, m;
                    if (bl->incl[blk] < 0 || bl->incl[blk] > l) {
                        tree_encode(&c->incl, &c->wr, leaf, l + 1);
                        continue;
                    }
                    if (bl->incl[blk] == l) {
                        tree_encode(&c->incl, &c->wr, leaf, l + 1);
                        tree_encode(&c->zbp, &c->wr, leaf, INF);
                    } else {
                        put_bit(&c->wr, bl->npasses[at] ? 1 : 0);
                    }
                    if (!bl->npasses[at])
                        continue;
                    n = bl->npasses[at];
                    write_npasses(&c->wr, n);
                    log_n = floorlog2((uint64_t)n);
                    need = (floorlog2((uint64_t)bl->length[at]) + 1) - log_n;
                    inc = need - (int)c->lblock[blk];
                    if (inc < 0)
                        inc = 0;
                    /* Unary Lblock increment: inc one bits followed by a zero. */
                    for (m = 0; m < inc; m++)
                        put_bit(&c->wr, 1);
                    put_bit(&c->wr, 0);
                    c->lblock[blk] += inc;
                    put_bits(&c->wr, (uint64_t)bl->length[at], (int)c->lblock[blk] + log_n);
                    c->contrib[ncontrib++] = (int32_t)blk;
                }
        }
        flush_header(&c->wr);
        for (i = 0; i < ncontrib; i++) {
            size_t at = (size_t)c->contrib[i] * nlayers + l;
            size_t seg = (size_t)bl->length[at];
            reserve(&c->wr, seg);
            if (seg)
                memcpy(c->wr.out + c->wr.size, data + bl->offset[at], seg);
            c->wr.size += seg;
        }
        packet_lengths[pkt] = c->wr.size - start;
    }
    *out = c->wr.out;
    *out_size = c->wr.size;
    c->wr.out = NULL;
    context_free(c);
    return 0;
}
