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

/* Tag-tree bases: bands without code-blocks get none. */
static size_t tree_bases(const hv_tables *t, size_t *base) {
    size_t b, total = 0;
    for (b = 0; b < t->nbands; b++) {
        base[b] = total;
        if (t->band_start[b + 1] > t->band_start[b])
            total += tree_size(t->band_w[b], t->band_h[b]);
    }
    return total;
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

int hv_read_packets(const uint8_t *data, size_t size, const int64_t *tile_part_ends,
                    size_t nparts, int zero_psot, int sop, int eph,
                    const hv_tables *t, hv_blocks *bl, char *error, size_t error_size) {
    context *const c = context_new(error, error_size);
    reader rd;
    size_t pkt, part = 0, pos = 0, b, k, nodes;
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

    c->base = allocate(&c->f, t->nbands, sizeof *c->base);
    c->lblock = allocate(&c->f, bl->nblocks, sizeof *c->lblock);
    c->contrib = allocate(&c->f, bl->nblocks, sizeof *c->contrib);
    nodes = tree_bases(t, c->base);
    trees_alloc(&c->f, &c->incl, nodes);
    trees_alloc(&c->f, &c->zbp, nodes);
    for (b = 0; b < t->nbands; b++)
        if (t->band_start[b + 1] > t->band_start[b]) {
            tree_init(&c->incl, c->base[b], t->band_w[b], t->band_h[b]);
            tree_init(&c->zbp, c->base[b], t->band_w[b], t->band_h[b]);
        }
    for (k = 0; k < bl->nblocks; k++)
        c->lblock[k] = 3;

    for (pkt = 0; pkt < t->npackets; pkt++) {
        int32_t prc = t->order_prc[pkt], l = t->order_layer[pkt];
        int ncontrib = 0, i;
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
            for (b = (size_t)t->prc_band_start[prc]; b < (size_t)t->prc_band_start[prc + 1]; b++) {
                int32_t first = t->band_start[b], last = t->band_start[b + 1], j;
                for (j = first; j < last; j++) {
                    size_t leaf = (size_t)(j - first), at;
                    int32_t blk = t->blk_ids[j];
                    int n;
                    int64_t len_bits;
                    if (bl->incl[blk] < 0) {
                        if (!tree_decode(&c->incl, &rd, c->base[b] + leaf, l + 1))
                            continue;
                        tree_decode(&c->zbp, &rd, c->base[b] + leaf, INF);
                        bl->zbp[blk] = c->zbp.value[c->base[b] + leaf];
                        bl->incl[blk] = l;
                    } else if (!read_bit(&rd)) {
                        continue;
                    }
                    n = read_npasses(&rd);
                    while (read_bit(&rd))
                        c->lblock[blk]++;
                    len_bits = c->lblock[blk] + floorlog2((uint64_t)n);
                    at = (size_t)blk * nlayers + l;
                    bl->npasses[at] = n;
                    bl->length[at] = read_bits(&rd, len_bits);
                    c->contrib[ncontrib++] = blk;
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

int hv_write_packets(const uint8_t *data, const hv_tables *t, const hv_blocks *bl,
                     uint8_t **out, size_t *out_size, uint64_t *packet_lengths,
                     char *error, size_t error_size) {
    context *const c = context_new(error, error_size);
    size_t pkt, b, k, nodes;
    int nlayers = bl->nlayers;

    if (c == NULL)
        return -1;
    if (setjmp(c->f.jump)) {
        context_free(c);
        return -1;
    }

    c->base = allocate(&c->f, t->nbands, sizeof *c->base);
    c->lblock = allocate(&c->f, bl->nblocks, sizeof *c->lblock);
    c->contrib = allocate(&c->f, bl->nblocks, sizeof *c->contrib);
    nodes = tree_bases(t, c->base);
    trees_alloc(&c->f, &c->incl, nodes);
    trees_alloc(&c->f, &c->zbp, nodes);
    for (b = 0; b < t->nbands; b++) {
        int32_t first = t->band_start[b], last = t->band_start[b + 1], j;
        if (last <= first)
            continue;
        tree_init(&c->incl, c->base[b], t->band_w[b], t->band_h[b]);
        tree_init(&c->zbp, c->base[b], t->band_w[b], t->band_h[b]);
        for (j = first; j < last; j++) {
            size_t leaf = (size_t)(j - first);
            int32_t blk = t->blk_ids[j];
            c->incl.value[c->base[b] + leaf] = bl->incl[blk] >= 0 ? bl->incl[blk] : nlayers;
            c->zbp.value[c->base[b] + leaf] = bl->zbp[blk] >= 0 ? bl->zbp[blk] : 0;
        }
        k = tree_size(t->band_w[b], t->band_h[b]);
        tree_propagate(&c->incl, c->base[b], k);
        tree_propagate(&c->zbp, c->base[b], k);
    }
    for (k = 0; k < bl->nblocks; k++)
        c->lblock[k] = 3;

    for (pkt = 0; pkt < t->npackets; pkt++) {
        int32_t prc = t->order_prc[pkt], l = t->order_layer[pkt];
        size_t start = c->wr.size;
        int ncontrib = 0, i;
        /* Like Kakadu, never use the one-bit empty packet; always write the
         * inclusion bits, even when no code-block contributes. */
        put_bit(&c->wr, 1);
        for (b = (size_t)t->prc_band_start[prc]; b < (size_t)t->prc_band_start[prc + 1]; b++) {
            int32_t first = t->band_start[b], last = t->band_start[b + 1], j;
            for (j = first; j < last; j++) {
                size_t leaf = (size_t)(j - first), at;
                int32_t blk = t->blk_ids[j];
                int n, log_n, need, inc, m;
                at = (size_t)blk * nlayers + l;
                if (bl->incl[blk] < 0 || bl->incl[blk] > l) {
                    tree_encode(&c->incl, &c->wr, c->base[b] + leaf, l + 1);
                    continue;
                }
                if (bl->incl[blk] == l) {
                    tree_encode(&c->incl, &c->wr, c->base[b] + leaf, l + 1);
                    tree_encode(&c->zbp, &c->wr, c->base[b] + leaf, INF);
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
                c->contrib[ncontrib++] = blk;
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
