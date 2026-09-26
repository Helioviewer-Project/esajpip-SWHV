/* transcode.c: see transcode.h. A port of hvJP2K's
 * jp2_precincts.transcode_codestream: the codestream is read with
 * hv_reader, laid out twice with hv_geometry (the input's precincts and
 * order, then the new ones), and written with hv_writer. */
#include "transcode.h"

#include <stdlib.h>
#include <string.h>

#include "hv_error.h"
#include "hv_reader.h"
#include "tier2.h"

/* Memory bounds of one transcode, for what a header can declare with
 * little or no data behind it. Each component-resolution takes 360 bytes
 * of layout (checked before it is allocated), each code-block under 100
 * bytes of state and tag-tree nodes, each output packet 20 bytes (its
 * place in the order and its PLT length) and at least a byte of output,
 * and each output precinct 64 bytes more (40 to put the packets in order,
 * 24 to index its tag trees). The input's packets need no bound of their
 * own: each takes at least a byte of its tile's data (B.10.3). Each
 * contribution of a code-block to a layer that they signal takes 32
 * bytes. */
#define MAX_RESOLUTIONS 65536
#define MAX_CODE_BLOCKS 250000
#define MAX_OUTPUT_PACKETS 2000000
/* The same, as the messages state them. */
#define MAX_RESOLUTIONS_TEXT "65,536"
#define MAX_CODE_BLOCKS_TEXT "250,000"
#define MAX_OUTPUT_PACKETS_TEXT "2,000,000"

/* Code-block styles (T.800 Table A.19) whose coding passes end where the
 * packet headers do not say: not supported. */
enum { CB_BYPASS = 0x01, CB_TERMALL = 0x04 };

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

/* Nonzero if the tile has more than `limit` packets, nprecincts * layers:
 * compared by division, which cannot overflow (nprecincts saturates). */
static int more_packets(const hv_geometry *g, uint64_t limit) {
    return g->layers > 0 && g->nprecincts > limit / (uint64_t)g->layers;
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
    char *error;
    size_t error_size;
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
 * PLT is not used, so its padding is accepted. COC and POC would change the
 * packet layout and PPM moves the packet headers, so they are rejected
 * whatever the flags; RGN changes neither and is kept. */
static int read_codestream(transcode *t, size_t start, size_t end, int ppx, int ppy,
                           unsigned flags, hv_out *out) {
    hv_item item;
    int status = hv_codestream_open(&t->cs, t->buf, start, end, HV_ACCEPT_PLT_PADDING | flags);

    if (status == 0 && hv_write_marker(out, HV_SOC) != 0)
        return hv_fail(t->error, t->error_size, "%s", out->error);
    while (status == 0 && (status = hv_codestream_next(&t->cs, &item)) == 1) {
        status = 0;
        switch (item.kind) {
        case HV_SEGMENT:
            if (item.code == HV_COC || item.code == HV_POC || item.code == HV_PPM)
                return hv_fail(t->error, t->error_size, "unsupported main header marker 0x%04X",
                               item.code);
            if (item.code == HV_COD) {
                t->cod = new_cod(item.cod, ppx, ppy);
                if (hv_write_cod(out, &t->cod) != 0)
                    return hv_fail(t->error, t->error_size, "%s", out->error);
            } else if (item.code != HV_TLM && item.code != HV_PLM &&
                       hv_write_bytes(out, t->buf + item.start, item.end - item.start) != 0) {
                return hv_fail(t->error, t->error_size, "%s", out->error);
            }
            break;
        case HV_TILE_SEGMENT:
            if (item.code != HV_PLT && item.code != HV_COM)
                return hv_fail(t->error, t->error_size, "unsupported tile-part marker 0x%04X",
                               item.code);
            break;
        case HV_TILE_PART:
            t->zero_psot |= item.sot.psot == 0;
            break;
        case HV_TILE_DATA:
            if (grow(&t->parts, &t->parts_cap, t->nparts + 1, sizeof *t->parts) != 0)
                return hv_fail(t->error, t->error_size, "out of memory");
            t->parts[t->nparts].start = item.start;
            t->parts[t->nparts++].end = item.end;
            break;
        case HV_END:
            return 0;
        }
    }
    return status < 0 ? hv_fail(t->error, t->error_size, "%s at %zu", t->cs.error, t->cs.error_at)
                      : 0;
}

/* Lays out the tile with the input's precincts and order and decodes its
 * packet headers. */
static int read_packets(transcode *t) {
    const Cod *cod = hv_codestream_cod(&t->cs);
    const hv_siz *siz = hv_codestream_siz(&t->cs);
    hv_tile_data tile = {t->buf, t->parts, t->nparts, t->zero_psot};
    size_t data = 0, i;

    if (t->cs.tiles != 1)
        return hv_fail(t->error, t->error_size, "only single-tile codestreams are supported");
    if (cod->spcod.cbStyle & (CB_BYPASS | CB_TERMALL))
        return hv_fail(t->error, t->error_size,
                       "code-block styles with bypass or termall are not supported");
    for (i = 0; i < t->nparts; i++)
        data += t->parts[i].end - t->parts[i].start;
    /* Empty components and resolutions have no packets, so the data does
     * not bound them. */
    if ((uint64_t)siz->fixed->csiz * (cod->spcod.levels + 1) > MAX_RESOLUTIONS)
        return hv_fail(t->error, t->error_size,
                       "component and resolution count exceeds supported limit ("
                       MAX_RESOLUTIONS_TEXT ")");
    if (hv_geometry_init(&t->in, siz, cod, 0, t->error, t->error_size) != 0)
        return -1;
    if (more_packets(&t->in, data))
        return hv_fail(t->error, t->error_size, "packet count exceeds tile data");
    if (t->in.nblocks > MAX_CODE_BLOCKS)
        return hv_fail(t->error, t->error_size,
                       "code-block count exceeds supported limit (" MAX_CODE_BLOCKS_TEXT ")");
    if (hv_geometry_packets(&t->in, &t->in_packets, &t->in_count, t->error,
                            t->error_size) != 0)
        return -1;
    if (hv_codeblocks_init(&t->cb, (size_t)t->in.nblocks) != 0)
        return hv_fail(t->error, t->error_size, "out of memory");
    return hv_read_packets(&t->in, t->in_packets, t->in_count, &tile, cod->scod.sopMarkers,
                           cod->scod.ephMarkers, &t->cb, t->error, t->error_size);
}

/* Lays out the tile with the new COD and writes it as one tile-part. Code-
 * block numbers are the same in both layouts while the code-block
 * partition is. The PLT needs the packet lengths, so the packets are
 * encoded twice, first for their lengths, then into the output. */
static int write_tile(transcode *t, int ppx, int ppy, hv_out *out) {
    size_t tp_start;

    /* The same code-block partition means the same code-blocks, so their
     * count is already bounded. */
    if (hv_geometry_init(&t->out, hv_codestream_siz(&t->cs), &t->cod.body, 0, t->error,
                         t->error_size) != 0)
        return -1;
    if (!same_partition(&t->in, &t->out))
        return hv_fail(t->error, t->error_size, "%dx%d precincts change the code-block partition",
                       1 << ppx, 1 << ppy);
    if (more_packets(&t->out, MAX_OUTPUT_PACKETS))
        return hv_fail(t->error, t->error_size,
                       "output packet count exceeds supported limit (" MAX_OUTPUT_PACKETS_TEXT ")");
    if (hv_geometry_packets(&t->out, &t->out_packets, &t->out_count, t->error,
                            t->error_size) != 0)
        return -1;
    if ((t->lengths = malloc((t->out_count + 1) * sizeof *t->lengths)) == NULL)
        return hv_fail(t->error, t->error_size, "out of memory");
    if (hv_write_packets(&t->out, t->out_packets, t->out_count, t->buf, &t->cb, NULL,
                         t->lengths, t->error, t->error_size) != 0)
        return -1;
    if (hv_begin_tile_part(out, 0, 0, 1, &tp_start) != 0 ||
        hv_write_plt(out, t->lengths, t->out_count) != 0 ||
        hv_write_marker(out, HV_SOD) != 0)
        return hv_fail(t->error, t->error_size, "%s", out->error);
    if (hv_write_packets(&t->out, t->out_packets, t->out_count, t->buf, &t->cb, out,
                         t->lengths, t->error, t->error_size) != 0)
        return -1;
    if (hv_end_tile_part(out, tp_start) != 0 || hv_write_marker(out, HV_EOC) != 0)
        return hv_fail(t->error, t->error_size, "%s", out->error);
    return 0;
}

int hv_transcode_codestream(const uint8_t *buf, size_t start, size_t end, int ppx, int ppy,
                            unsigned flags, hv_out *out, char *error, size_t error_size) {
    size_t out_start = out->size;
    transcode t;
    int status;

    memset(&t, 0, sizeof t);
    t.error = error;
    t.error_size = error_size;
    t.buf = buf;
    error[0] = 0;
    if (out->error != NULL)                     /* an earlier write failed */
        return hv_fail(t.error, t.error_size, "%s", out->error);
    if (flags != 0 && flags != HV_PROFILE_HEADERS)
        status = hv_fail(t.error, t.error_size,
                         "flags 0x%X: only 0 and HV_PROFILE_HEADERS are supported", flags);
    else if (ppx < 1 || ppx > 15 || ppy < 1 || ppy > 15)
        status = hv_fail(t.error, t.error_size,
                         "precinct dimensions must be powers of 2 from 2 to 32,768");
    else if ((status = read_codestream(&t, start, end, ppx, ppy, flags, out)) == 0 &&
             (status = read_packets(&t)) == 0)
        status = write_tile(&t, ppx, ppy, out);
    if (status != 0)
        hv_out_rewind(out, out_start);  /* nothing of a failed transcode */
    transcode_free(&t);
    return status;
}
