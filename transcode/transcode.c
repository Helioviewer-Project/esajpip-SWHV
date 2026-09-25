/* transcode.c: see transcode.h. A port of hvJP2K's
 * jp2_precincts.transcode_codestream: the codestream is read with
 * hv_reader, laid out twice with hv_geometry (the input's precincts and
 * order, then the new ones), and written with hv_writer. */
#include "transcode.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_reader.h"
#include "tier2.h"

/* Limits for one transcode, as in jp2_precincts.py (they bound the memory
 * for the per-code-block state), plus the int32 range of packet indices. */
#define MAX_CODE_BLOCKS 250000
#define MAX_BLOCK_LAYER_ENTRIES 2000000
#define MAX_PACKETS 0x7FFFFFFF

enum { COD = 0xFF52, COC = 0xFF53, TLM = 0xFF55, PLM = 0xFF57, PLT = 0xFF58,
       RGN = 0xFF5E, POC = 0xFF5F, PPM = 0xFF60, COM = 0xFF64,
       SOC = 0xFF4F, SOD = 0xFF93, EOC = 0xFFD9 };

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

/* Grows *p (elements of `size` bytes), which holds *cap, to hold `need`. */
static int grow(void *p, size_t *cap, size_t need, size_t size) {
    void **q = p, *r;
    size_t n = *cap ? *cap : 256;
    if (need <= *cap)
        return 0;
    while (n < need)
        n *= 2;
    if (n > SIZE_MAX / size || (r = realloc(*q, n * size)) == NULL)
        return -1;
    *q = r;
    *cap = n;
    return 0;
}

/* ------------------------------------------------------------------------
 * Limits and the code-block partition
 * ------------------------------------------------------------------------ */

/* The limits of one transcode, checked per resolution in the order
 * jp2_precincts._geometry checks them. packet_limit >= 0 bounds the packet
 * count: even an empty packet takes a byte of tile data. */
static int check_limits(const hv_geometry *g, int64_t packet_limit, errors *e) {
    int64_t packets = 0, blocks = 0, all_packets = 0;
    int c, r, b;
    for (c = 0; c < g->ncomps; c++)
        for (r = 0; r <= g->levels; r++) {
            const hv_resolution *res = hv_geometry_res(g, c, r);
            int64_t n = res->pw, m = res->ph;
            if (n == 0 || m == 0)
                continue;
            if (packet_limit >= 0) {
                if (n > packet_limit || m > packet_limit / n ||
                    n * m > (packet_limit - packets) / g->layers)
                    return fail(e, "packet count exceeds tile data");
                packets += n * m * g->layers;
            }
            for (b = 0; b < res->nbands; b++) {
                const hv_band *band = &res->band[b];
                if (band->nx > MAX_CODE_BLOCKS || band->ny > MAX_CODE_BLOCKS ||
                    band->nx * band->ny > MAX_CODE_BLOCKS - blocks)
                    return fail(e, "code-block count exceeds supported limit");
                blocks += band->nx * band->ny;
                if (blocks * g->layers > MAX_BLOCK_LAYER_ENTRIES)
                    return fail(e, "code-block layer count exceeds supported limit");
            }
            /* hv_geometry_packets indexes packets with int32. */
            if (n > MAX_PACKETS || m > MAX_PACKETS / n ||
                n * m > (MAX_PACKETS - all_packets) / g->layers)
                return fail(e, "packet count exceeds supported limit");
            all_packets += n * m * g->layers;
        }
    return 0;
}

/* Nonzero if every non-empty band has the same code-blocks in both. */
static int same_partition(const hv_geometry *a, const hv_geometry *b) {
    size_t nres = (size_t)a->ncomps * (a->levels + 1), i;
    int k;
    for (i = 0; i < nres; i++)
        for (k = 0; k < a->res[i].nbands; k++) {
            const hv_band *x = &a->res[i].band[k], *y = &b->res[i].band[k];
            if (y->nx > 0 && y->ny > 0 && (x->xcb != y->xcb || x->ycb != y->ycb))
                return 0;
        }
    return 1;
}

/* ------------------------------------------------------------------------
 * Codestream (transcode_codestream)
 * ------------------------------------------------------------------------ */

/* The new COD: the input's, with the given precincts at every resolution,
 * RPCL order, and no SOP or EPH markers. */
static CodSegment_Std new_cod(const CodSegment_Std *in, int ppx, int ppy) {
    CodSegment_Std cod = *in;
    int r;
    cod.body.scod.customPrecincts = TRUE;
    cod.body.scod.sopMarkers = FALSE;
    cod.body.scod.ephMarkers = FALSE;
    cod.body.sgcod.progression = HV_RPCL;
    cod.body.spcod.precincts.nCount = (int)in->body.spcod.levels + 1;
    for (r = 0; r <= (int)in->body.spcod.levels; r++) {
        cod.body.spcod.precincts.arr[r].ppx = ppx;
        cod.body.spcod.precincts.arr[r].ppy = ppy;
    }
    return cod;
}

int hv_transcode_codestream(const uint8_t *buf, size_t start, size_t end, int ppx, int ppy,
                            hv_out *out, char *error, size_t error_size) {
    errors e = {error, error_size};
    size_t out_start = out->size, nparts = 0, parts_cap = 0, old_count = 0, new_count = 0;
    hv_codestream cs;
    hv_item item;
    hv_span *parts = NULL;
    hv_geometry old_g, new_g;
    hv_packet *old_packets = NULL, *new_packets = NULL;
    hv_codeblocks cb;
    CodSegment_Std cod;
    uint64_t *lengths = NULL;
    size_t tp_start;
    int zero_psot = 0, status;
    const Siz *siz;

    error[0] = 0;
    memset(&cs, 0, sizeof cs);
    memset(&old_g, 0, sizeof old_g);
    memset(&new_g, 0, sizeof new_g);
    memset(&cb, 0, sizeof cb);
    memset(&cod, 0, sizeof cod);
    if (ppx < 1 || ppx > 15 || ppy < 1 || ppy > 15) {
        status = fail(&e, "precinct dimensions must be powers of 2 from 2 to 32768");
        goto done;
    }

    /* Read the codestream. The main header is written as it is read, with
     * the new COD in place of the old one and without TLM and PLM, which
     * transcoding makes stale; the other segments are copied. The input's
     * PLT is not used, so its padding is accepted. */
    status = hv_codestream_open(&cs, buf, start, end, HV_ACCEPT_PLT_PADDING);
    if (status == 0 && hv_write_marker(out, SOC) != 0)
        status = fail(&e, "%s", out->error);
    while (status == 0 && (status = hv_codestream_next(&cs, &item)) == 1) {
        status = 0;
        switch (item.kind) {
        case HV_SEGMENT:
            if (item.code == COC || item.code == POC || item.code == PPM || item.code == RGN)
                status = fail(&e, "unsupported main header marker 0x%04X", item.code);
            else if (item.code == COD) {
                cod = new_cod(item.cod, ppx, ppy);
                if (hv_write_cod(out, &cod) != 0)
                    status = fail(&e, "%s", out->error);
            } else if (item.code != TLM && item.code != PLM &&
                       hv_write_bytes(out, buf + item.start, item.end - item.start) != 0)
                status = fail(&e, "%s", out->error);
            break;
        case HV_TILE_SEGMENT:
            if (item.code != PLT && item.code != COM)
                status = fail(&e, "unsupported tile-part marker 0x%04X", item.code);
            break;
        case HV_TILE_PART:
            zero_psot |= item.sot.psot == 0;
            break;
        case HV_TILE_DATA:
            if (grow(&parts, &parts_cap, nparts + 1, sizeof *parts) != 0) {
                status = fail(&e, "out of memory");
                break;
            }
            parts[nparts].start = item.start;
            parts[nparts++].end = item.end;
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
    if (cs.tiles != 1) {
        status = fail(&e, "only single-tile codestreams are supported");
        goto done;
    }
    if (hv_codestream_cod(&cs)->spcod.cbStyle & 0x05) {
        status = fail(&e, "code-block styles with bypass or termall are not supported");
        goto done;
    }

    /* Decode the packet headers with the input's precincts and order. */
    {
        size_t data = 0, i;
        for (i = 0; i < nparts; i++)
            data += parts[i].end - parts[i].start;
        if ((status = hv_geometry_init(&old_g, siz, hv_codestream_cod(&cs), 0, error,
                                       error_size)) != 0 ||
            (status = check_limits(&old_g, (int64_t)data, &e)) != 0 ||
            (status = hv_geometry_packets(&old_g, &old_packets, &old_count, error,
                                          error_size)) != 0)
            goto done;
    }
    if (hv_codeblocks_init(&cb, (size_t)old_g.nblocks) != 0) {
        status = fail(&e, "out of memory");
        goto done;
    }
    if ((status = hv_read_packets(&old_g, old_packets, old_count, buf, parts, nparts,
                                  zero_psot, hv_codestream_cod(&cs)->scod.sopMarkers,
                                  hv_codestream_cod(&cs)->scod.ephMarkers, &cb, error,
                                  error_size)) != 0)
        goto done;

    /* Lay out the new precincts. Code-block numbers are the same in both
     * geometries while the code-block partition is. */
    if ((status = hv_geometry_init(&new_g, siz, &cod.body, 0, error, error_size)) != 0 ||
        (status = check_limits(&new_g, -1, &e)) != 0)
        goto done;
    if (!same_partition(&old_g, &new_g)) {
        status = fail(&e, "%dx%d precincts change the code-block partition", 1 << ppx,
                      1 << ppy);
        goto done;
    }
    if ((status = hv_geometry_packets(&new_g, &new_packets, &new_count, error,
                                      error_size)) != 0)
        goto done;
    if ((lengths = malloc((new_count + 1) * sizeof *lengths)) == NULL) {
        status = fail(&e, "out of memory");
        goto done;
    }

    /* One tile-part: the PLT needs the packet lengths, so the packets are
     * encoded twice, first for their lengths, then into the output. */
    if ((status = hv_write_packets(&new_g, new_packets, new_count, buf, &cb, NULL, lengths,
                                   error, error_size)) != 0)
        goto done;
    if (hv_begin_tile_part(out, 0, 0, 1, &tp_start) != 0 ||
        hv_write_plt(out, lengths, new_count) != 0 ||
        hv_write_marker(out, SOD) != 0) {
        status = fail(&e, "%s", out->error);
        goto done;
    }
    if ((status = hv_write_packets(&new_g, new_packets, new_count, buf, &cb, out, lengths,
                                   error, error_size)) != 0)
        goto done;
    if (hv_end_tile_part(out, tp_start) != 0 || hv_write_marker(out, EOC) != 0)
        status = fail(&e, "%s", out->error);

done:
    if (status != 0 && out->size > out_start)
        out->size = out_start;  /* nothing of a failed transcode */
    hv_codestream_close(&cs);
    hv_geometry_free(&old_g);
    hv_geometry_free(&new_g);
    hv_codeblocks_free(&cb);
    free(parts);
    free(old_packets);
    free(new_packets);
    free(lengths);
    return status < 0 ? -1 : 0;
}
