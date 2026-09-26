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

/* Memory bounds of one transcode, for what a header can declare with
 * little or no data behind it. Each component-resolution takes 360 bytes
 * of layout (checked before it is allocated), each code-block under 100
 * bytes of state and tag-tree nodes, each output packet 20 bytes (its
 * place in the order and its PLT length) and at least a byte of output.
 * The input's packets need no bound of their own: each takes at least a
 * byte of its tile's data (B.10.3). */
#define MAX_RESOLUTIONS 65536
#define MAX_CODE_BLOCKS 250000
#define MAX_OUTPUT_PACKETS 2000000

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

/* The tile's packet count, saturating. */
static uint64_t packet_count(const hv_geometry *g) {
    return g->nprecincts > UINT64_MAX / (uint64_t)g->layers
           ? UINT64_MAX : g->nprecincts * (uint64_t)g->layers;
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

/* One transcode: the input as read, both layouts of its tile, and the
 * code-blocks' contributions. */
typedef struct {
    errors e;
    const uint8_t *buf;
    hv_codestream cs;
    hv_span *parts;             /* the tile-parts' data */
    size_t nparts, parts_cap;
    int zero_psot;
    CodSegment_Std cod;         /* the new COD */
    hv_geometry in, out;        /* the input's and the new layout */
    hv_packet *in_packets, *out_packets;
    size_t in_count, out_count;
    hv_codeblocks cb;
    uint64_t *lengths;          /* of the new packets */
} transcode;

static void transcode_free(transcode *t) {
    hv_codestream_close(&t->cs);
    hv_geometry_free(&t->in);
    hv_geometry_free(&t->out);
    hv_codeblocks_free(&t->cb);
    free(t->parts);
    free(t->in_packets);
    free(t->out_packets);
    free(t->lengths);
}

/* Reads the codestream in buf[start, end) with hv_reader. The main header
 * is written as it is read, with the new COD in place of the old one and
 * without TLM and PLM, which transcoding makes stale; its other segments
 * are copied. The tile-parts' data is recorded where it is. The input's
 * PLT is not used, so its padding is accepted. */
static int read_codestream(transcode *t, size_t start, size_t end, int ppx, int ppy,
                           hv_out *out) {
    hv_item item;
    int status = hv_codestream_open(&t->cs, t->buf, start, end, HV_ACCEPT_PLT_PADDING);

    if (status == 0 && hv_write_marker(out, SOC) != 0)
        return fail(&t->e, "%s", out->error);
    while (status == 0 && (status = hv_codestream_next(&t->cs, &item)) == 1) {
        status = 0;
        switch (item.kind) {
        case HV_SEGMENT:
            if (item.code == COC || item.code == POC || item.code == PPM || item.code == RGN)
                return fail(&t->e, "unsupported main header marker 0x%04X", item.code);
            if (item.code == COD) {
                t->cod = new_cod(item.cod, ppx, ppy);
                if (hv_write_cod(out, &t->cod) != 0)
                    return fail(&t->e, "%s", out->error);
            } else if (item.code != TLM && item.code != PLM &&
                       hv_write_bytes(out, t->buf + item.start, item.end - item.start) != 0) {
                return fail(&t->e, "%s", out->error);
            }
            break;
        case HV_TILE_SEGMENT:
            if (item.code != PLT && item.code != COM)
                return fail(&t->e, "unsupported tile-part marker 0x%04X", item.code);
            break;
        case HV_TILE_PART:
            t->zero_psot |= item.sot.psot == 0;
            break;
        case HV_TILE_DATA:
            if (grow(&t->parts, &t->parts_cap, t->nparts + 1, sizeof *t->parts) != 0)
                return fail(&t->e, "out of memory");
            t->parts[t->nparts].start = item.start;
            t->parts[t->nparts++].end = item.end;
            break;
        case HV_END:
            return 0;
        }
    }
    return status < 0 ? fail(&t->e, "%s", t->cs.error) : 0;
}

/* Lays out the tile with the input's precincts and order and decodes its
 * packet headers. */
static int read_packets(transcode *t) {
    const Cod *cod = hv_codestream_cod(&t->cs);
    hv_tile_data tile = {t->buf, t->parts, t->nparts, t->zero_psot};
    size_t data = 0, i;

    if (t->cs.tiles != 1)
        return fail(&t->e, "only single-tile codestreams are supported");
    if (cod->spcod.cbStyle & 0x05)
        return fail(&t->e, "code-block styles with bypass or termall are not supported");
    for (i = 0; i < t->nparts; i++)
        data += t->parts[i].end - t->parts[i].start;
    /* Empty components and resolutions have no packets, so the data does
     * not bound them. */
    if ((uint64_t)hv_codestream_siz(&t->cs)->csiz * (cod->spcod.levels + 1) > MAX_RESOLUTIONS)
        return fail(&t->e, "component and resolution count exceeds supported limit");
    if (hv_geometry_init(&t->in, hv_codestream_siz(&t->cs), cod, 0, t->e.error,
                         t->e.error_size) != 0)
        return -1;
    if (packet_count(&t->in) > data)
        return fail(&t->e, "packet count exceeds tile data");
    if (t->in.nblocks > MAX_CODE_BLOCKS)
        return fail(&t->e, "code-block count exceeds supported limit");
    if (hv_geometry_packets(&t->in, &t->in_packets, &t->in_count, t->e.error,
                            t->e.error_size) != 0)
        return -1;
    if (hv_codeblocks_init(&t->cb, (size_t)t->in.nblocks) != 0)
        return fail(&t->e, "out of memory");
    return hv_read_packets(&t->in, t->in_packets, t->in_count, &tile, cod->scod.sopMarkers,
                           cod->scod.ephMarkers, &t->cb, t->e.error, t->e.error_size);
}

/* Lays out the tile with the new COD and writes it as one tile-part. Code-
 * block numbers are the same in both layouts while the code-block
 * partition is. The PLT needs the packet lengths, so the packets are
 * encoded twice, first for their lengths, then into the output. */
static int write_tile(transcode *t, int ppx, int ppy, hv_out *out) {
    size_t tp_start;

    /* The same code-block partition means the same code-blocks, so their
     * count is already bounded. */
    if (hv_geometry_init(&t->out, hv_codestream_siz(&t->cs), &t->cod.body, 0, t->e.error,
                         t->e.error_size) != 0)
        return -1;
    if (!same_partition(&t->in, &t->out))
        return fail(&t->e, "%dx%d precincts change the code-block partition", 1 << ppx,
                    1 << ppy);
    if (packet_count(&t->out) > MAX_OUTPUT_PACKETS)
        return fail(&t->e, "output packet count exceeds supported limit");
    if (hv_geometry_packets(&t->out, &t->out_packets, &t->out_count, t->e.error,
                            t->e.error_size) != 0)
        return -1;
    if ((t->lengths = malloc((t->out_count + 1) * sizeof *t->lengths)) == NULL)
        return fail(&t->e, "out of memory");
    if (hv_write_packets(&t->out, t->out_packets, t->out_count, t->buf, &t->cb, NULL,
                         t->lengths, t->e.error, t->e.error_size) != 0)
        return -1;
    if (hv_begin_tile_part(out, 0, 0, 1, &tp_start) != 0 ||
        hv_write_plt(out, t->lengths, t->out_count) != 0 ||
        hv_write_marker(out, SOD) != 0)
        return fail(&t->e, "%s", out->error);
    if (hv_write_packets(&t->out, t->out_packets, t->out_count, t->buf, &t->cb, out,
                         t->lengths, t->e.error, t->e.error_size) != 0)
        return -1;
    if (hv_end_tile_part(out, tp_start) != 0 || hv_write_marker(out, EOC) != 0)
        return fail(&t->e, "%s", out->error);
    return 0;
}

int hv_transcode_codestream(const uint8_t *buf, size_t start, size_t end, int ppx, int ppy,
                            hv_out *out, char *error, size_t error_size) {
    size_t out_start = out->size;
    transcode t;
    int status;

    memset(&t, 0, sizeof t);
    t.e.error = error;
    t.e.error_size = error_size;
    t.buf = buf;
    error[0] = 0;
    if (ppx < 1 || ppx > 15 || ppy < 1 || ppy > 15)
        status = fail(&t.e, "precinct dimensions must be powers of 2 from 2 to 32768");
    else if ((status = read_codestream(&t, start, end, ppx, ppy, out)) == 0 &&
             (status = read_packets(&t)) == 0)
        status = write_tile(&t, ppx, ppy, out);
    if (status != 0)
        out->size = out_start;  /* nothing of a failed transcode */
    transcode_free(&t);
    return status;
}
