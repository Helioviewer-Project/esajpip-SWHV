/* transcode.c: see transcode.h. The geometry, packet order and table
 * layout follow hvJP2K's jp2_precincts.py (_geometry, _packet_order,
 * _flatten, _transcode_packets, transcode_codestream); the codestream is
 * read with hv_reader and written with hv_writer. */
#include "transcode.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_reader.h"
#include "tier2.h"

/* Limits for one transcode, as in jp2_precincts.py, plus the int32 range
 * of the packet tables. */
#define MAX_CODE_BLOCKS 250000
#define MAX_BLOCK_LAYER_ENTRIES 2000000
#define MAX_PACKETS 0x7FFFFFFF

enum { COD = 0xFF52, COC = 0xFF53, TLM = 0xFF55, PLM = 0xFF57, PLT = 0xFF58,
       RGN = 0xFF5E, POC = 0xFF5F, PPM = 0xFF60, COM = 0xFF64,
       SOC = 0xFF4F, SOD = 0xFF93, EOC = 0xFFD9 };
enum { LRCP, RLCP, RPCL, PCRL, CPRL };

typedef struct {
    char *error;
    size_t error_size;
} errors;

static int fail(errors *e, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(e->error, e->error_size, format, args);
    va_end(args);
    return -1;
}

/* b > 0. C division truncates, which is the ceiling for a <= 0. */
static int64_t ceildiv(int64_t a, int64_t b) {
    return a > 0 ? (a + b - 1) / b : a / b;
}

static int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }
static uint64_t max64u(uint64_t a, uint64_t b) { return a > b ? a : b; }

/* The capacity for `need` elements: `cap`, doubled from 256 until enough. */
static size_t next_cap(size_t cap, size_t need) {
    size_t n = cap ? cap : 256;
    if (need <= cap)
        return cap;
    while (n < need)
        n *= 2;
    return n;
}

/* Reallocates *p to n elements of `size` bytes. */
static int resize(void *p, size_t n, size_t size) {
    void **q = p, *r;
    if (n > SIZE_MAX / size || (r = realloc(*q, n * size)) == NULL)
        return -1;
    *q = r;
    return 0;
}

/* Grows *p, which holds *cap elements, to hold at least `need`. */
static int grow(void *p, size_t *cap, size_t need, size_t size) {
    size_t n = next_cap(*cap, need);
    if (n != *cap) {
        if (resize(p, n, size) != 0)
            return -1;
        *cap = n;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * Geometry (_geometry, _flatten)
 * ------------------------------------------------------------------------ */

/* What the geometry needs from SIZ and COD. */
typedef struct {
    int64_t tx0, ty0, tx1, ty1;
    int ncomps, levels, layers, cbw, cbh;
    const int *xr, *yr;
} params;

/* One band of one (component, resolution): extent and code-block grid.
 * Code-blocks are numbered per band in raster order of this grid, from
 * `base`, so both partitions of a codestream name the same blocks. */
typedef struct {
    int64_t bx0, by0, bx1, by1;
    int xcb, ycb;
    int64_t gx0, gy0, nx, ny;
    int64_t base;
} band_info;

/* The precinct grid of one (component, resolution). */
typedef struct {
    int PPx, PPy, bPPx, bPPy;
    int64_t s, npx0, npy0, npx1, npy1;
} res_info;

typedef struct {
    int c, r;
    uint64_t ax, ay;            /* anchor on the reference grid (B.12) */
} precinct;

typedef struct {
    res_info *res;              /* per (c, r) */
    band_info *bands;           /* per (c, r, band) */
    size_t *first;              /* per (c, r): its first precinct; one more */
    precinct *prc;              /* in (component, resolution, raster) order */
    size_t nprc, prc_cap;       /* prc and t.prc_band_start (prc_cap + 1) */
    size_t band_cap;            /* t.band_w, t.band_h and t.band_start (+ 1) */
    size_t blk_cap;             /* t.blk_ids */
    int64_t nblocks;
    hv_tables t;
} geometry;

static void geometry_free(geometry *g) {
    free(g->res);
    free(g->bands);
    free(g->first);
    free(g->prc);
    free(g->t.band_w);
    free(g->t.band_h);
    free(g->t.band_start);
    free(g->t.blk_ids);
    free(g->t.prc_band_start);
    free(g->t.order_prc);
    free(g->t.order_layer);
    memset(g, 0, sizeof *g);
}

static size_t cr_index(const params *p, int c, int r) {
    return (size_t)c * (p->levels + 1) + r;
}

static band_info *band_at(const params *p, const geometry *g, int c, int r, int b) {
    return &g->bands[cr_index(p, c, r) * 4 + b];
}

/* Appends one precinct band: a w x h grid of code-blocks from (cbx0, cby0). */
static int add_band(geometry *g, const band_info *bi, int64_t cbx0, int64_t cby0,
                    int64_t w, int64_t h) {
    size_t nb = g->t.nbands, nids = (size_t)g->t.band_start[nb];
    size_t cap = next_cap(g->band_cap, nb + 1);
    int64_t cx, cy;
    if (cap != g->band_cap) {
        if (resize(&g->t.band_w, cap, sizeof *g->t.band_w) != 0 ||
            resize(&g->t.band_h, cap, sizeof *g->t.band_h) != 0 ||
            resize(&g->t.band_start, cap + 1, sizeof *g->t.band_start) != 0)
            return -1;
        g->band_cap = cap;
    }
    if (grow(&g->t.blk_ids, &g->blk_cap, nids + (size_t)(w * h), sizeof *g->t.blk_ids) != 0)
        return -1;
    for (cy = cby0; cy < cby0 + h; cy++)
        for (cx = cbx0; cx < cbx0 + w; cx++)
            g->t.blk_ids[nids++] = (int32_t)(bi->base + (cy - bi->gy0) * bi->nx + (cx - bi->gx0));
    g->t.band_w[nb] = (int32_t)w;
    g->t.band_h[nb] = (int32_t)h;
    g->t.band_start[nb + 1] = (int32_t)nids;
    g->t.nbands = nb + 1;
    return 0;
}

/* Appends a precinct whose bands were added last. */
static int add_precinct(geometry *g, int c, int r, uint64_t ax, uint64_t ay) {
    size_t n = g->nprc, cap = next_cap(g->prc_cap, n + 1);
    if (cap != g->prc_cap) {
        if (resize(&g->prc, cap, sizeof *g->prc) != 0 ||
            resize(&g->t.prc_band_start, cap + 1, sizeof *g->t.prc_band_start) != 0)
            return -1;
        g->prc_cap = cap;
    }
    g->prc[n].c = c;
    g->prc[n].r = r;
    g->prc[n].ax = ax;
    g->prc[n].ay = ay;
    g->t.prc_band_start[n + 1] = (int32_t)g->t.nbands;
    g->nprc = n + 1;
    return 0;
}

/* The precincts of every (component, resolution) for precinct exponents
 * pp[r], with their bands and code-block ids. packet_limit >= 0 bounds the
 * packet count: even an empty packet takes a byte of tile data. With a
 * `reference`, code-block ids are the reference's, and a different
 * code-block partition is an error.
 *
 * The first pass lays out the grids and checks the limits, the second
 * builds the precincts. The limits are checked in the order
 * jp2_precincts.py checks them, the partition after all of them. */
static int build_geometry(const params *p, const int (*pp)[2], int64_t packet_limit,
                          const geometry *reference, geometry *g, errors *e) {
    int NL = p->levels, c, r, b, mismatch = 0;
    int64_t packets = 0, blocks = 0, all_packets = 0;
    size_t ncr = (size_t)p->ncomps * (NL + 1);

    memset(g, 0, sizeof *g);
    g->res = calloc(ncr, sizeof *g->res);
    g->bands = calloc(ncr * 4, sizeof *g->bands);
    g->first = calloc(ncr + 1, sizeof *g->first);
    g->t.band_start = calloc(1, sizeof *g->t.band_start);
    g->t.prc_band_start = calloc(1, sizeof *g->t.prc_band_start);
    if (g->res == NULL || g->bands == NULL || g->first == NULL ||
        g->t.band_start == NULL || g->t.prc_band_start == NULL)
        return fail(e, "out of memory");

    for (c = 0; c < p->ncomps; c++) {
        int64_t xr = p->xr[c], yr = p->yr[c];
        int64_t cx0 = ceildiv(p->tx0, xr), cy0 = ceildiv(p->ty0, yr);
        int64_t cx1 = ceildiv(p->tx1, xr), cy1 = ceildiv(p->ty1, yr);
        for (r = 0; r <= NL; r++) {
            res_info *ri = &g->res[cr_index(p, c, r)];
            int first_b = r ? 1 : 0, last_b = r ? 3 : 0, xcb, ycb;
            int64_t rx0, ry0, rx1, ry1;

            ri->PPx = pp[r][0];
            ri->PPy = pp[r][1];
            ri->bPPx = r ? ri->PPx - 1 : ri->PPx;
            ri->bPPy = r ? ri->PPy - 1 : ri->PPy;
            ri->s = (int64_t)1 << (NL - r);
            rx0 = ceildiv(cx0, ri->s);
            ry0 = ceildiv(cy0, ri->s);
            rx1 = ceildiv(cx1, ri->s);
            ry1 = ceildiv(cy1, ri->s);
            /* Precincts smaller than the code-blocks clip them. */
            xcb = p->cbw < ri->bPPx ? p->cbw : ri->bPPx;
            ycb = p->cbh < ri->bPPy ? p->cbh : ri->bPPy;
            for (b = first_b; b <= last_b; b++) {
                band_info *bi = band_at(p, g, c, r, b);
                int nb = r ? NL - r + 1 : NL, xo = b & 1, yo = b >> 1;
                int64_t o = r ? (int64_t)1 << (nb - 1) : 0, step = (int64_t)1 << nb;
                bi->bx0 = ceildiv(cx0 - o * xo, step);
                bi->by0 = ceildiv(cy0 - o * yo, step);
                bi->bx1 = ceildiv(cx1 - o * xo, step);
                bi->by1 = ceildiv(cy1 - o * yo, step);
                bi->xcb = xcb;
                bi->ycb = ycb;
                bi->gx0 = bi->bx0 >> xcb;
                bi->gy0 = bi->by0 >> ycb;
                if (bi->bx1 > bi->bx0 && bi->by1 > bi->by0) {
                    bi->nx = ceildiv(bi->bx1, (int64_t)1 << xcb) - bi->gx0;
                    bi->ny = ceildiv(bi->by1, (int64_t)1 << ycb) - bi->gy0;
                }
            }
            if (rx1 > rx0 && ry1 > ry0) {
                int64_t n, m;
                ri->npx0 = rx0 >> ri->PPx;
                ri->npy0 = ry0 >> ri->PPy;
                ri->npx1 = ceildiv(rx1, (int64_t)1 << ri->PPx);
                ri->npy1 = ceildiv(ry1, (int64_t)1 << ri->PPy);
                n = ri->npx1 - ri->npx0;
                m = ri->npy1 - ri->npy0;
                if (packet_limit >= 0) {
                    if (n > packet_limit || m > packet_limit / n ||
                        n * m > (packet_limit - packets) / p->layers)
                        return fail(e, "packet count exceeds tile data");
                    packets += n * m * p->layers;
                }
                /* The block grid aligns with the precinct grid, so each band
                 * can be counted before building any of its precincts. */
                for (b = first_b; b <= last_b; b++) {
                    const band_info *bi = band_at(p, g, c, r, b);
                    if (bi->nx > MAX_CODE_BLOCKS || bi->ny > MAX_CODE_BLOCKS ||
                        bi->nx * bi->ny > MAX_CODE_BLOCKS - blocks)
                        return fail(e, "code-block count exceeds supported limit");
                    blocks += bi->nx * bi->ny;
                    if (blocks * p->layers > MAX_BLOCK_LAYER_ENTRIES)
                        return fail(e, "code-block layer count exceeds supported limit");
                }
                /* The tables index packets with int32. */
                if (n > MAX_PACKETS || m > MAX_PACKETS / n ||
                    n * m > (MAX_PACKETS - all_packets) / p->layers)
                    return fail(e, "packet count exceeds supported limit");
                all_packets += n * m * p->layers;
            }
            for (b = first_b; b <= last_b; b++) {
                band_info *bi = band_at(p, g, c, r, b);
                if (reference != NULL) {
                    const band_info *ref = band_at(p, reference, c, r, b);
                    if (bi->nx * bi->ny > 0 && (ref->xcb != bi->xcb || ref->ycb != bi->ycb))
                        mismatch = 1;
                    bi->base = ref->base;
                } else {
                    bi->base = g->nblocks;
                    g->nblocks += bi->nx * bi->ny;
                }
            }
        }
    }
    if (mismatch)
        return fail(e, "%dx%d precincts change the code-block partition",
                    1 << pp[0][0], 1 << pp[0][1]);
    if (reference != NULL)
        g->nblocks = reference->nblocks;

    for (c = 0; c < p->ncomps; c++) {
        uint64_t xr = (uint64_t)p->xr[c], yr = (uint64_t)p->yr[c];
        for (r = 0; r <= NL; r++) {
            const res_info *ri = &g->res[cr_index(p, c, r)];
            int first_b = r ? 1 : 0, last_b = r ? 3 : 0;
            int64_t px, py;
            g->first[cr_index(p, c, r)] = g->nprc;
            for (py = ri->npy0; py < ri->npy1; py++) {
                for (px = ri->npx0; px < ri->npx1; px++) {
                    uint64_t ax = ((uint64_t)px << ri->PPx) * xr * (uint64_t)ri->s;
                    uint64_t ay = ((uint64_t)py << ri->PPy) * yr * (uint64_t)ri->s;
                    for (b = first_b; b <= last_b; b++) {
                        const band_info *bi = band_at(p, g, c, r, b);
                        /* The precinct's region in band coordinates. */
                        int64_t qx0 = max64(bi->bx0, px << ri->bPPx);
                        int64_t qy0 = max64(bi->by0, py << ri->bPPy);
                        int64_t qx1 = min64(bi->bx1, (px + 1) << ri->bPPx);
                        int64_t qy1 = min64(bi->by1, (py + 1) << ri->bPPy);
                        int64_t cbx0 = 0, cby0 = 0, w = 0, h = 0;
                        if (qx1 > qx0 && qy1 > qy0) {
                            cbx0 = qx0 >> bi->xcb;
                            cby0 = qy0 >> bi->ycb;
                            w = ceildiv(qx1, (int64_t)1 << bi->xcb) - cbx0;
                            h = ceildiv(qy1, (int64_t)1 << bi->ycb) - cby0;
                        }
                        if (add_band(g, bi, cbx0, cby0, w, h) != 0)
                            return fail(e, "out of memory");
                    }
                    if (add_precinct(g, c, r, max64u(ax, (uint64_t)p->tx0),
                                     max64u(ay, (uint64_t)p->ty0)) != 0)
                        return fail(e, "out of memory");
                }
            }
        }
    }
    g->first[ncr] = g->nprc;
    return 0;
}

/* ------------------------------------------------------------------------
 * Progression order (_packet_order, B.12)
 * ------------------------------------------------------------------------ */

typedef struct {
    uint64_t key[4];
    size_t index;
} sort_entry;

static int compare_entries(const void *a, const void *b) {
    const sort_entry *p = a, *q = b;
    int i;
    for (i = 0; i < 4; i++)
        if (p->key[i] != q->key[i])
            return p->key[i] < q->key[i] ? -1 : 1;
    /* Python's sort is stable: equal keys keep geometry order. */
    return p->index < q->index ? -1 : p->index > q->index;
}

static int build_order(const params *p, geometry *g, int order, errors *e) {
    size_t n = g->nprc * (size_t)p->layers, k = 0, i;
    int l, r, c, outer, inner;

    g->t.order_prc = malloc((n ? n : 1) * sizeof *g->t.order_prc);
    g->t.order_layer = malloc((n ? n : 1) * sizeof *g->t.order_layer);
    if (g->t.order_prc == NULL || g->t.order_layer == NULL)
        return fail(e, "out of memory");
    g->t.npackets = n;
    g->t.nprecincts = g->nprc;
    if (order == LRCP || order == RLCP) {
        /* COC is unsupported, so every component has the same resolutions. */
        int nouter = order == LRCP ? p->layers : p->levels + 1;
        int ninner = order == LRCP ? p->levels + 1 : p->layers;
        for (outer = 0; outer < nouter; outer++)
            for (inner = 0; inner < ninner; inner++) {
                l = order == LRCP ? outer : inner;
                r = order == LRCP ? inner : outer;
                for (c = 0; c < p->ncomps; c++) {
                    size_t cr = (size_t)c * (p->levels + 1) + r;
                    for (i = g->first[cr]; i < g->first[cr + 1]; i++) {
                        g->t.order_prc[k] = (int32_t)i;
                        g->t.order_layer[k++] = l;
                    }
                }
            }
        return 0;
    }
    {
        /* Layers come last; (component, resolution, anchor) orders precincts. */
        sort_entry *sorted = malloc((g->nprc ? g->nprc : 1) * sizeof *sorted);
        if (sorted == NULL)
            return fail(e, "out of memory");
        for (i = 0; i < g->nprc; i++) {
            const precinct *prc = &g->prc[i];
            uint64_t rpcl[4] = {(uint64_t)prc->r, prc->ay, prc->ax, (uint64_t)prc->c};
            uint64_t pcrl[4] = {prc->ay, prc->ax, (uint64_t)prc->c, (uint64_t)prc->r};
            uint64_t cprl[4] = {(uint64_t)prc->c, prc->ay, prc->ax, (uint64_t)prc->r};
            memcpy(sorted[i].key, order == RPCL ? rpcl : order == PCRL ? pcrl : cprl,
                   sizeof sorted[i].key);
            sorted[i].index = i;
        }
        qsort(sorted, g->nprc, sizeof *sorted, compare_entries);
        for (i = 0; i < g->nprc; i++)
            for (l = 0; l < p->layers; l++) {
                g->t.order_prc[k] = (int32_t)sorted[i].index;
                g->t.order_layer[k++] = l;
            }
        free(sorted);
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * Codestream (transcode_codestream)
 * ------------------------------------------------------------------------ */

/* The tile-parts' data, concatenated, and each part's end in it. */
typedef struct {
    uint8_t *data;
    size_t size, cap;
    int64_t *ends;
    size_t nparts, ends_cap;
} tile_data;

static int append_data(tile_data *d, const uint8_t *bytes, size_t size) {
    if (grow(&d->data, &d->cap, d->size + size + 1, 1) != 0 ||
        grow(&d->ends, &d->ends_cap, d->nparts + 1, sizeof *d->ends) != 0)
        return -1;
    memcpy(d->data + d->size, bytes, size);
    d->size += size;
    d->ends[d->nparts++] = (int64_t)d->size;
    return 0;
}

int hv_transcode_codestream(const uint8_t *buf, size_t start, size_t end, int ppx, int ppy,
                            hv_out *out, char *error, size_t error_size) {
    errors e = {error, error_size};
    hv_codestream cs;
    hv_item item;
    hv_out head[2];
    tile_data body;
    geometry old_g, new_g;
    hv_blocks blocks;
    params p;
    int (*pp_old)[2] = NULL, (*pp_new)[2] = NULL;
    int *xr = NULL, *yr = NULL, seen_cod = 0, zero_psot = 0, status, r;
    uint8_t *packets = NULL;
    uint64_t *lengths = NULL;
    size_t packets_size = 0, i, n, tp_start;
    const Siz *siz;
    const Cod *cod;
    CodSegment_Std new_cod;

    memset(&new_cod, 0, sizeof new_cod);

    error[0] = 0;
    memset(&cs, 0, sizeof cs);
    memset(&body, 0, sizeof body);
    memset(&old_g, 0, sizeof old_g);
    memset(&new_g, 0, sizeof new_g);
    memset(&blocks, 0, sizeof blocks);
    hv_out_init(&head[0]);
    hv_out_init(&head[1]);
    if (ppx < 1 || ppx > 15 || ppy < 1 || ppy > 15) {
        status = fail(&e, "precinct dimensions must be powers of 2 from 2 to 32768");
        goto done;
    }

    /* Read the codestream. The input's PLT is not used, so its padding is
     * accepted. */
    status = hv_codestream_open(&cs, buf, start, end, HV_ACCEPT_PLT_PADDING);
    while (status == 0 && (status = hv_codestream_next(&cs, &item)) == 1) {
        status = 0;
        switch (item.kind) {
        case HV_SEGMENT:
            if (item.code == COC || item.code == POC || item.code == PPM || item.code == RGN)
                status = fail(&e, "unsupported main header marker 0x%04X", item.code);
            else if (item.code == COD) {
                new_cod = *item.cod;
                seen_cod = 1;
            } else if (item.code != TLM && item.code != PLM &&  /* stale after transcoding */
                       hv_write_bytes(&head[seen_cod], buf + item.start,
                                      item.end - item.start) != 0)
                status = fail(&e, "%s", head[seen_cod].error);
            break;
        case HV_TILE_SEGMENT:
            if (item.code != PLT && item.code != COM)
                status = fail(&e, "unsupported tile-part marker 0x%04X", item.code);
            break;
        case HV_TILE_PART:
            zero_psot |= item.sot.psot == 0;
            break;
        case HV_TILE_DATA:
            if (append_data(&body, buf + item.start, item.end - item.start) != 0)
                status = fail(&e, "out of memory");
            break;
        case HV_END:
            break;
        }
        if (item.kind == HV_END)
            break;
    }
    if (status < 0) {
        if (error[0] == 0)
            fail(&e, "%s", cs.error);
        goto done;
    }
    siz = hv_codestream_siz(&cs);
    cod = hv_codestream_cod(&cs);
    if (cs.tiles != 1) {
        status = fail(&e, "only single-tile codestreams are supported");
        goto done;
    }
    if (cod->spcod.cbStyle & 0x05) {
        status = fail(&e, "code-block styles with bypass or termall are not supported");
        goto done;
    }

    /* The geometry parameters, and the old and new precinct exponents. */
    p.ncomps = (int)siz->csiz;
    p.levels = (int)cod->spcod.levels;
    p.layers = (int)cod->sgcod.layers;
    p.cbw = (int)cod->spcod.cbWidthExp + 2;
    p.cbh = (int)cod->spcod.cbHeightExp + 2;
    p.tx0 = max64((int64_t)siz->xtosiz, (int64_t)siz->xosiz);
    p.ty0 = max64((int64_t)siz->ytosiz, (int64_t)siz->yosiz);
    p.tx1 = min64((int64_t)(siz->xtosiz + siz->xtsiz), (int64_t)siz->xsiz);
    p.ty1 = min64((int64_t)(siz->ytosiz + siz->ytsiz), (int64_t)siz->ysiz);
    xr = malloc(p.ncomps * sizeof *xr);
    yr = malloc(p.ncomps * sizeof *yr);
    pp_old = malloc((p.levels + 1) * sizeof *pp_old);
    pp_new = malloc((p.levels + 1) * sizeof *pp_new);
    if (xr == NULL || yr == NULL || pp_old == NULL || pp_new == NULL) {
        status = fail(&e, "out of memory");
        goto done;
    }
    for (i = 0; i < (size_t)p.ncomps; i++) {
        xr[i] = (int)siz->components.arr[i].xrsiz;
        yr[i] = (int)siz->components.arr[i].yrsiz;
    }
    p.xr = xr;
    p.yr = yr;
    for (r = 0; r <= p.levels; r++) {
        pp_old[r][0] = cod->scod.customPrecincts ? (int)cod->spcod.precincts.arr[r].ppx : 15;
        pp_old[r][1] = cod->scod.customPrecincts ? (int)cod->spcod.precincts.arr[r].ppy : 15;
        pp_new[r][0] = ppx;
        pp_new[r][1] = ppy;
    }

    /* Decode the packets with the input's precincts and order. */
    if ((status = build_geometry(&p, (const int (*)[2])pp_old, (int64_t)body.size, NULL,
                                 &old_g, &e)) != 0 ||
        (status = build_order(&p, &old_g, (int)cod->sgcod.progression, &e)) != 0)
        goto done;
    blocks.nblocks = (size_t)old_g.nblocks;
    blocks.nlayers = p.layers;
    n = blocks.nblocks * (size_t)p.layers + 1;
    blocks.zbp = malloc((blocks.nblocks + 1) * sizeof *blocks.zbp);
    blocks.incl = malloc((blocks.nblocks + 1) * sizeof *blocks.incl);
    blocks.npasses = calloc(n, sizeof *blocks.npasses);
    blocks.offset = calloc(n, sizeof *blocks.offset);
    blocks.length = calloc(n, sizeof *blocks.length);
    if (blocks.zbp == NULL || blocks.incl == NULL || blocks.npasses == NULL ||
        blocks.offset == NULL || blocks.length == NULL) {
        status = fail(&e, "out of memory");
        goto done;
    }
    for (i = 0; i < blocks.nblocks; i++)
        blocks.zbp[i] = blocks.incl[i] = -1;
    if ((status = hv_read_packets(body.data, body.size, body.ends, body.nparts, zero_psot,
                                  cod->scod.sopMarkers, cod->scod.ephMarkers, &old_g.t,
                                  &blocks, error, error_size)) != 0)
        goto done;

    /* Encode them with the new precincts in RPCL order. */
    if ((status = build_geometry(&p, (const int (*)[2])pp_new, -1, &old_g, &new_g, &e)) != 0 ||
        (status = build_order(&p, &new_g, RPCL, &e)) != 0)
        goto done;
    lengths = malloc((new_g.t.npackets + 1) * sizeof *lengths);
    if (lengths == NULL) {
        status = fail(&e, "out of memory");
        goto done;
    }
    if ((status = hv_write_packets(body.data, &new_g.t, &blocks, &packets, &packets_size,
                                   lengths, error, error_size)) != 0)
        goto done;

    /* COD with the new precincts and order; SOP and EPH are not written.
     * The rest of the main header is copied as read. */
    new_cod.body.scod.customPrecincts = TRUE;
    new_cod.body.scod.sopMarkers = FALSE;
    new_cod.body.scod.ephMarkers = FALSE;
    new_cod.body.sgcod.progression = RPCL;
    new_cod.body.spcod.precincts.nCount = p.levels + 1;
    for (r = 0; r <= p.levels; r++) {
        new_cod.body.spcod.precincts.arr[r].ppx = ppx;
        new_cod.body.spcod.precincts.arr[r].ppy = ppy;
    }

    /* SOC, the main header with COD in its place, one tile-part, EOC. */
    if (hv_write_marker(out, SOC) != 0 ||
        hv_write_bytes(out, head[0].data, head[0].size) != 0 ||
        hv_write_cod(out, &new_cod) != 0 ||
        hv_write_bytes(out, head[1].data, head[1].size) != 0 ||
        hv_begin_tile_part(out, 0, 0, 1, &tp_start) != 0 ||
        hv_write_plt(out, lengths, new_g.t.npackets) != 0 ||
        hv_write_marker(out, SOD) != 0 ||
        hv_write_bytes(out, packets, packets_size) != 0 ||
        hv_end_tile_part(out, tp_start) != 0 ||
        hv_write_marker(out, EOC) != 0)
        status = fail(&e, "%s", out->error);

done:
    hv_codestream_close(&cs);
    hv_out_free(&head[0]);
    hv_out_free(&head[1]);
    geometry_free(&old_g);
    geometry_free(&new_g);
    free(body.data);
    free(body.ends);
    free(blocks.zbp);
    free(blocks.incl);
    free(blocks.npasses);
    free(blocks.offset);
    free(blocks.length);
    free(packets);
    free(lengths);
    free(xr);
    free(yr);
    free(pp_old);
    free(pp_new);
    return status < 0 ? -1 : 0;
}
