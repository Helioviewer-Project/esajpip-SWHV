/* hv_geometry.h: the resolutions, bands, precincts and code-blocks of one
 * tile, and its packets in progression order, from the decoded SIZ and COD
 * (T.800 B.2 to B.7 and B.12).
 *
 * The partition is computed, not stored: precincts and code-blocks are
 * counted per resolution and band, and the code-blocks of one precinct are
 * derived when asked for. Code-blocks are numbered per band in raster order
 * of the band's code-block grid, bands in (component, resolution, band)
 * order. Two precinct partitions of a tile that keep its code-block
 * partition therefore give every code-block the same number.
 *
 * One COD applies to every component (no COC), and the progression has no
 * POC. Counts saturate at 2^64 - 1. */
#ifndef HV_GEOMETRY_H
#define HV_GEOMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "jpeg2000-io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Progression orders, Table A.16. */
enum { HV_LRCP, HV_RLCP, HV_RPCL, HV_PCRL, HV_CPRL };

/* One band of one resolution (B.5, B.7). */
typedef struct {
    int64_t x0, y0, x1, y1;     /* extent in band coordinates */
    int xcb, ycb;               /* code-block exponents, clipped by the precincts */
    int64_t gx0, gy0;           /* code-block grid: first cell */
    int64_t nx, ny;             /* code-block grid: cells; 0 for an empty band */
    uint64_t first_block;       /* number of the band's first code-block */
} hv_band;

/* One resolution of one component (B.5, B.6). */
typedef struct {
    int c, r;
    int ppx, ppy;               /* precinct exponents */
    int bppx, bppy;             /* the same in band coordinates */
    int64_t x0, y0, x1, y1;     /* extent in resolution coordinates */
    int64_t px0, py0;           /* precinct grid: first cell */
    int64_t pw, ph;             /* precinct grid: cells; 0 for an empty resolution */
    uint64_t xstep, ystep;      /* reference grid samples per resolution sample */
    uint64_t first_precinct;    /* number of the resolution's first precinct */
    int nbands;                 /* 1 (LL) at r = 0, else 3 (HL, LH, HH) */
    hv_band band[3];
} hv_resolution;

typedef struct {
    int64_t tx0, ty0, tx1, ty1; /* the tile on the reference grid */
    int ncomps, levels, layers, progression;
    hv_resolution *res;         /* ncomps * (levels + 1), component-major */
    uint64_t nprecincts;
    uint64_t nblocks;
} hv_geometry;

/* One packet: a precinct of one resolution, and a layer. */
typedef struct {
    uint32_t resolution;        /* index into res */
    uint32_t precinct;          /* raster index in that resolution's precinct grid */
    uint16_t layer;
} hv_packet;

/* A precinct's code-blocks in one band: w x h cells of the band's grid
 * from (cx0, cy0). */
typedef struct {
    int64_t cx0, cy0, w, h;
} hv_block_rect;

/* Lays out tile `tile` of `siz` with the coding style `cod`. 0, or -1 with
 * a message in error. Call hv_geometry_free in both cases. Allocates one
 * hv_resolution per component and resolution, csiz * (levels + 1) of them
 * (up to 540,672), whether or not they hold data: callers that read
 * untrusted headers bound that product first. */
int hv_geometry_init(hv_geometry *g, const Siz *siz, const Cod *cod, uint32_t tile,
                     char *error, size_t error_size);
void hv_geometry_free(hv_geometry *g);

static inline const hv_resolution *hv_geometry_res(const hv_geometry *g, int c, int r) {
    return &g->res[(size_t)c * (g->levels + 1) + r];
}

/* Precinct `index` (raster) of res: its cell in the precinct grid. */
static inline void hv_precinct_cell(const hv_resolution *res, uint64_t index,
                                    int64_t *px, int64_t *py) {
    *px = res->px0 + (int64_t)(index % (uint64_t)res->pw);
    *py = res->py0 + (int64_t)(index / (uint64_t)res->pw);
}

/* The code-blocks of precinct (px, py) in res->band[b]; w = h = 0 when the
 * precinct does not cover the band. */
hv_block_rect hv_precinct_blocks(const hv_resolution *res, int b, int64_t px, int64_t py);

/* The number of code-block (cx, cy) of a band. */
static inline uint64_t hv_block_number(const hv_band *band, int64_t cx, int64_t cy) {
    return band->first_block + (uint64_t)((cy - band->gy0) * band->nx + (cx - band->gx0));
}

/* The packets in the progression order of the COD. *packets is malloc'd.
 * Fails when there are more than 2^31 - 1 packets. */
int hv_geometry_packets(const hv_geometry *g, hv_packet **packets, size_t *count,
                        char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
