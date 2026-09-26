/* hv_geometry.c: see hv_geometry.h. Equation numbers are T.800's. */
#include "hv_geometry.h"

#include "hv_error.h"

#include <stdlib.h>
#include <string.h>

/* The most packets hv_geometry_packets lays out (hv_geometry.h). */
#define MAX_PACKETS 2147483647u

/* b > 0. C division truncates, which is the ceiling for a <= 0. */
static int64_t ceildiv(int64_t a, int64_t b) {
    return a > 0 ? (a + b - 1) / b : a / b;
}

static int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }

static uint64_t sat_add(uint64_t a, uint64_t b) {
    return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

static uint64_t sat_mul(uint64_t a, uint64_t b) {
    return a != 0 && b > UINT64_MAX / a ? UINT64_MAX : a * b;
}

int hv_geometry_init(hv_geometry *g, const hv_siz *hsiz, const Cod *cod, uint32_t tile,
                     char *error, size_t error_size) {
    const SizFixed *siz = hsiz->fixed;
    int64_t ntx, nty, p, q;
    int NL = (int)cod->spcod.levels, c, r, b;
    int cbw = (int)cod->spcod.cbWidthExp + 2, cbh = (int)cod->spcod.cbHeightExp + 2;

    memset(g, 0, sizeof *g);
    /* The components are read up to Csiz: there must be that many. */
    if (hsiz->ncomponents != siz->csiz)
        return hv_fail(error, error_size, "siz.csiz-count");
    /* B-5 to B-10: the tile's position in the grid and its extent. */
    ntx = ceildiv((int64_t)siz->xsiz - (int64_t)siz->xtosiz, (int64_t)siz->xtsiz);
    nty = ceildiv((int64_t)siz->ysiz - (int64_t)siz->ytosiz, (int64_t)siz->ytsiz);
    if (ntx <= 0 || nty <= 0 || (int64_t)tile / ntx >= nty)   /* ntx * nty can overflow */
        return hv_fail(error, error_size, "tile %u outside the tile grid", (unsigned)tile);
    p = (int64_t)tile % ntx;
    q = (int64_t)tile / ntx;
    g->tx0 = max64((int64_t)siz->xtosiz + p * (int64_t)siz->xtsiz, (int64_t)siz->xosiz);
    g->ty0 = max64((int64_t)siz->ytosiz + q * (int64_t)siz->ytsiz, (int64_t)siz->yosiz);
    g->tx1 = min64((int64_t)siz->xtosiz + (p + 1) * (int64_t)siz->xtsiz, (int64_t)siz->xsiz);
    g->ty1 = min64((int64_t)siz->ytosiz + (q + 1) * (int64_t)siz->ytsiz, (int64_t)siz->ysiz);
    g->ncomps = (int)siz->csiz;
    g->levels = NL;
    g->layers = (int)cod->sgcod.layers;
    g->progression = (int)cod->sgcod.progression;
    g->res = calloc((size_t)g->ncomps * (NL + 1), sizeof *g->res);
    if (g->res == NULL)
        return hv_fail(error, error_size, "out of memory");

    for (c = 0; c < g->ncomps; c++) {
        int64_t xr = (int64_t)hsiz->components[c].xrsiz;
        int64_t yr = (int64_t)hsiz->components[c].yrsiz;
        /* B-12: the tile-component. */
        int64_t cx0 = ceildiv(g->tx0, xr), cy0 = ceildiv(g->ty0, yr);
        int64_t cx1 = ceildiv(g->tx1, xr), cy1 = ceildiv(g->ty1, yr);
        for (r = 0; r <= NL; r++) {
            hv_resolution *res = &g->res[(size_t)c * (NL + 1) + r];
            int64_t s = (int64_t)1 << (NL - r);
            /* Precincts smaller than the code-blocks clip them (B-17). */
            int xcb, ycb;

            res->c = c;
            res->r = r;
            res->ppx = cod->scod.customPrecincts ? (int)cod->spcod.precincts.arr[r].ppx : 15;
            res->ppy = cod->scod.customPrecincts ? (int)cod->spcod.precincts.arr[r].ppy : 15;
            res->bppx = r ? res->ppx - 1 : res->ppx;
            res->bppy = r ? res->ppy - 1 : res->ppy;
            xcb = cbw < res->bppx ? cbw : res->bppx;
            ycb = cbh < res->bppy ? cbh : res->bppy;
            /* B-14 */
            res->x0 = ceildiv(cx0, s);
            res->y0 = ceildiv(cy0, s);
            res->x1 = ceildiv(cx1, s);
            res->y1 = ceildiv(cy1, s);
            res->xstep = (uint64_t)xr * (uint64_t)s;
            res->ystep = (uint64_t)yr * (uint64_t)s;
            res->nbands = r ? 3 : 1;
            for (b = 0; b < res->nbands; b++) {
                hv_band *band = &res->band[b];
                /* Band type: 0 LL, 1 HL, 2 LH, 3 HH; bit 0 x, bit 1 y. */
                int type = r ? b + 1 : 0, xo = type & 1, yo = type >> 1;
                int nb = r ? NL - r + 1 : NL;
                int64_t o = r ? (int64_t)1 << (nb - 1) : 0, step = (int64_t)1 << nb;
                /* B-15 */
                band->x0 = ceildiv(cx0 - o * xo, step);
                band->y0 = ceildiv(cy0 - o * yo, step);
                band->x1 = ceildiv(cx1 - o * xo, step);
                band->y1 = ceildiv(cy1 - o * yo, step);
                band->xcb = xcb;
                band->ycb = ycb;
                band->gx0 = band->x0 >> xcb;
                band->gy0 = band->y0 >> ycb;
                if (band->x1 > band->x0 && band->y1 > band->y0) {
                    band->nx = ceildiv(band->x1, (int64_t)1 << xcb) - band->gx0;
                    band->ny = ceildiv(band->y1, (int64_t)1 << ycb) - band->gy0;
                }
                band->first_block = g->nblocks;
                g->nblocks = sat_add(g->nblocks,
                                     sat_mul((uint64_t)band->nx, (uint64_t)band->ny));
            }
            /* B-16 */
            if (res->x1 > res->x0 && res->y1 > res->y0) {
                res->px0 = res->x0 >> res->ppx;
                res->py0 = res->y0 >> res->ppy;
                res->pw = ceildiv(res->x1, (int64_t)1 << res->ppx) - res->px0;
                res->ph = ceildiv(res->y1, (int64_t)1 << res->ppy) - res->py0;
            }
            res->first_precinct = g->nprecincts;
            g->nprecincts = sat_add(g->nprecincts,
                                    sat_mul((uint64_t)res->pw, (uint64_t)res->ph));
        }
    }
    return 0;
}

void hv_geometry_free(hv_geometry *g) {
    free(g->res);
    memset(g, 0, sizeof *g);
}

hv_block_rect hv_precinct_blocks(const hv_resolution *res, int b, int64_t px, int64_t py) {
    const hv_band *band = &res->band[b];
    hv_block_rect rect = {0, 0, 0, 0};
    /* The precinct's region in band coordinates. */
    int64_t qx0 = max64(band->x0, px << res->bppx);
    int64_t qy0 = max64(band->y0, py << res->bppy);
    int64_t qx1 = min64(band->x1, (px + 1) << res->bppx);
    int64_t qy1 = min64(band->y1, (py + 1) << res->bppy);
    if (qx1 > qx0 && qy1 > qy0) {
        rect.cx0 = qx0 >> band->xcb;
        rect.cy0 = qy0 >> band->ycb;
        rect.w = ceildiv(qx1, (int64_t)1 << band->xcb) - rect.cx0;
        rect.h = ceildiv(qy1, (int64_t)1 << band->ycb) - rect.cy0;
    }
    return rect;
}

/* ------------------------------------------------------------------------
 * Progression order (B.12)
 * ------------------------------------------------------------------------ */

typedef struct {
    uint64_t key[4];
    uint32_t resolution, precinct;
} sort_entry;

static int compare_entries(const void *a, const void *b) {
    const sort_entry *p = a, *q = b;
    int i;
    for (i = 0; i < 4; i++)
        if (p->key[i] != q->key[i])
            return p->key[i] < q->key[i] ? -1 : 1;
    if (p->resolution != q->resolution)
        return p->resolution < q->resolution ? -1 : 1;
    return p->precinct < q->precinct ? -1 : p->precinct > q->precinct;
}

int hv_geometry_packets(const hv_geometry *g, hv_packet **packets, size_t *count,
                        char *error, size_t error_size) {
    uint64_t total = sat_mul(g->nprecincts, (uint64_t)g->layers);
    size_t nres = (size_t)g->ncomps * (g->levels + 1), k = 0, i, n;
    hv_packet *out;
    int l, r, c;

    *packets = NULL;
    *count = 0;
    if (total > MAX_PACKETS)
        return hv_fail(error, error_size, "more than 2,147,483,647 packets in the tile");
    if ((out = malloc((total ? total : 1) * sizeof *out)) == NULL)
        return hv_fail(error, error_size, "out of memory");

    if (g->progression == HV_LRCP || g->progression == HV_RLCP) {
        /* Every component has levels + 1 resolutions (no COC). */
        int lrcp = g->progression == HV_LRCP;
        int nouter = lrcp ? g->layers : g->levels + 1;
        int ninner = lrcp ? g->levels + 1 : g->layers;
        int outer, inner;
        for (outer = 0; outer < nouter; outer++)
            for (inner = 0; inner < ninner; inner++) {
                l = lrcp ? outer : inner;
                r = lrcp ? inner : outer;
                for (c = 0; c < g->ncomps; c++) {
                    size_t ri = (size_t)c * (g->levels + 1) + r;
                    n = (size_t)(g->res[ri].pw * g->res[ri].ph);
                    for (i = 0; i < n; i++) {
                        out[k].resolution = (uint32_t)ri;
                        out[k].precinct = (uint32_t)i;
                        out[k++].layer = (uint16_t)l;
                    }
                }
            }
    } else {
        /* Layers come last. Each precinct is visited at its anchor, the first
         * reference grid point of the precinct inside the tile; the other
         * loops are (component, resolution, anchor) in the order's nesting. */
        sort_entry *sorted = malloc((g->nprecincts ? g->nprecincts : 1) * sizeof *sorted);
        size_t ri, m = 0;
        if (sorted == NULL) {
            free(out);
            return hv_fail(error, error_size, "out of memory");
        }
        for (ri = 0; ri < nres; ri++) {
            const hv_resolution *res = &g->res[ri];
            n = (size_t)(res->pw * res->ph);
            for (i = 0; i < n; i++) {
                int64_t px, py;
                uint64_t ax, ay;
                hv_precinct_cell(res, i, &px, &py);
                ax = ((uint64_t)px << res->ppx) * res->xstep;
                ay = ((uint64_t)py << res->ppy) * res->ystep;
                if (ax < (uint64_t)g->tx0)
                    ax = (uint64_t)g->tx0;
                if (ay < (uint64_t)g->ty0)
                    ay = (uint64_t)g->ty0;
                if (g->progression == HV_RPCL) {
                    sorted[m].key[0] = (uint64_t)res->r;
                    sorted[m].key[1] = ay;
                    sorted[m].key[2] = ax;
                    sorted[m].key[3] = (uint64_t)res->c;
                } else if (g->progression == HV_PCRL) {
                    sorted[m].key[0] = ay;
                    sorted[m].key[1] = ax;
                    sorted[m].key[2] = (uint64_t)res->c;
                    sorted[m].key[3] = (uint64_t)res->r;
                } else {
                    sorted[m].key[0] = (uint64_t)res->c;
                    sorted[m].key[1] = ay;
                    sorted[m].key[2] = ax;
                    sorted[m].key[3] = (uint64_t)res->r;
                }
                sorted[m].resolution = (uint32_t)ri;
                sorted[m++].precinct = (uint32_t)i;
            }
        }
        qsort(sorted, m, sizeof *sorted, compare_entries);
        for (i = 0; i < m; i++)
            for (l = 0; l < g->layers; l++) {
                out[k].resolution = sorted[i].resolution;
                out[k].precinct = sorted[i].precinct;
                out[k++].layer = (uint16_t)l;
            }
        free(sorted);
    }
    *packets = out;
    *count = k;
    return 0;
}
