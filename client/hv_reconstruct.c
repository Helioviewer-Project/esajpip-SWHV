/* hv_reconstruct.c: see hv_reconstruct.h. */
#include "hv_reconstruct.h"

#include <string.h>

#include "jpeg2000/hv_error.h"
#include "jpeg2000/hv_grid.h"
#include "jpeg2000/hv_reader.h"

enum { COD = 0xFF52, TLM = 0xFF55, PLM = 0xFF57, RPCL = 2 };

typedef struct {
    uint8_t *out;
    size_t capacity, size;
} output;

static void put(output *o, const void *bytes, size_t n) {
    if (o->size <= o->capacity && n <= o->capacity - o->size)
        memcpy(o->out + o->size, bytes, n);
    o->size += n;
}

/* The main header without the segments that describe the layout the server
 * read (TLM, PLM), and with the progression this writes. */
static int put_main_header(output *o, hv_codestream *cs, const hv_bin *bin, char *error,
                           size_t error_size) {
    static const uint8_t soc[] = {0xFF, 0x4F};
    hv_item item;

    put(o, soc, sizeof soc);
    do {
        size_t at = o->size;
        int status = hv_codestream_next(cs, &item);
        if (status != 1)
            return hv_fail(error, error_size, "main header: %s",
                           status < 0 ? cs->error : "ends early");
        if (item.kind != HV_SEGMENT)
            return hv_fail(error, error_size, "main header data-bin holds more than the header");
        if (item.code == TLM || item.code == PLM)
            continue;
        put(o, bin->data + item.start, item.end - item.start);
        /* Marker, Lcod, Scod, then the progression order (Table A.12). */
        if (item.code == COD && o->size <= o->capacity)
            o->out[at + 5] = RPCL;
    } while (item.end < bin->length);
    return 0;
}

/* Opens the main header in bin, as the server reads one, and writes it to
 * o: 0 with *cod its COD and *precincts its number of precinct bins, or -1
 * with a message. cs is to be closed either way. */
static int read_main_header(hv_codestream *cs, const hv_bin *bin, output *o, const Cod **cod,
                            uint64_t *precincts, char *error, size_t error_size) {
    uint64_t packets;

    if (hv_codestream_open(cs, bin->data, 0, bin->length, HV_PROFILE) != 0)
        return hv_fail(error, error_size, "main header: %s", cs->error);
    if (put_main_header(o, cs, bin, error, error_size) != 0)
        return -1;
    if ((*cod = hv_codestream_cod(cs)) == NULL)
        return hv_fail(error, error_size, "main header has no COD");
    /* As the server counts them. */
    packets = hv_rule_packets(hv_codestream_siz(cs), &(*cod)->sgcod, &(*cod)->spcod);
    if (packets == 0)
        return hv_fail(error, error_size, "main header: more than 2,147,483,647 packets");
    *precincts = packets / (*cod)->sgcod.layers;
    return 0;
}

size_t hv_reconstruct(const hv_cache *cache, uint64_t codestream, uint8_t *out,
                      size_t capacity, char *error, size_t error_size) {
    static const uint8_t tile_part[] = {
        0xFF, 0x90, 0, 10, 0, 0, 0, 0, 0, 0, 0, 1,     /* SOT: tile 0, to EOC, part 0 of 1 */
        0xFF, 0x93};                                    /* SOD */
    static const uint8_t empty[] = {0x00, 0xFF, 0x92};  /* empty packet, EPH */
    static const uint8_t eoc[] = {0xFF, 0xD9};
    const hv_bin *main_header = hv_cache_find(cache, HV_BIN_MAIN_HEADER, codestream, 0);
    const hv_bin *tile_header = hv_cache_find(cache, HV_BIN_TILE_HEADER, codestream, 0);
    output o = {out, out != NULL ? capacity : 0, 0};
    hv_codestream cs;
    const Cod *cod;
    uint64_t precincts, id, layer;
    size_t size = 0;

    if (main_header == NULL || !main_header->complete) {
        hv_fail(error, error_size, "main header data-bin is %s",
                main_header == NULL ? "missing" : "incomplete");
        return 0;
    }
    if (tile_header != NULL && tile_header->length != 0) {
        hv_fail(error, error_size, "tile header data-bin is not empty");
        return 0;
    }
    if (read_main_header(&cs, main_header, &o, &cod, &precincts, error, error_size) != 0)
        goto done;

    /* A precinct's bin identifier is c + s * components, s its number among
     * the precincts of its component, from the lowest resolution, in raster
     * order (T.808 A.3.2.1). With one precinct grid for all components,
     * increasing identifiers are the RPCL order. */
    put(&o, tile_part, sizeof tile_part);
    for (id = 0; id < precincts; id++) {
        const hv_bin *bin = hv_cache_find(cache, HV_BIN_PRECINCT, codestream, id);
        if (bin != NULL && bin->complete) {
            put(&o, bin->data, bin->length);
        } else if (bin != NULL && bin->length != 0) {
            hv_fail(error, error_size, "precinct data-bin %llu is incomplete",
                    (unsigned long long)id);
            goto done;
        } else {
            for (layer = 0; layer < cod->sgcod.layers; layer++)
                put(&o, empty, cod->scod.ephMarkers ? 3 : 1);
        }
    }
    put(&o, eoc, sizeof eoc);
    size = o.size;

done:
    hv_codestream_close(&cs);
    return size;
}

int hv_reconstruct_status(const hv_cache *cache, uint64_t codestream, hv_status *status,
                          char *error, size_t error_size) {
    const hv_bin *main_header = hv_cache_find(cache, HV_BIN_MAIN_HEADER, codestream, 0);
    output none = {NULL, 0, 0};
    hv_codestream cs;
    const Cod *cod;
    uint64_t precincts, id = 0, end = 0;
    int result, r;

    memset(status, 0, sizeof *status);
    if (main_header == NULL || !main_header->complete)
        return 0;
    result = read_main_header(&cs, main_header, &none, &cod, &precincts, error, error_size);
    if (result == 0) {
        const SizFixed *siz = hv_codestream_siz(&cs)->fixed;
        status->width = siz->xsiz;
        status->height = siz->ysiz;
        status->components = (int)siz->csiz;
        status->resolutions = (int)cod->spcod.levels + 1;
        /* A level's precinct bins follow the lower levels' (see above), as
         * many as hv_rule_packets counts for it. */
        for (r = 0; r < status->resolutions && id == end; r++) {
            unsigned level = (unsigned)(status->resolutions - 1 - r);
            int custom = cod->spcod.precincts.nCount != 0;
            int ppx = custom ? (int)cod->spcod.precincts.arr[r].ppx : 15;
            int ppy = custom ? (int)cod->spcod.precincts.arr[r].ppy : 15;
            uint64_t w = hv_resolution_coord(siz->xsiz, level);
            uint64_t h = hv_resolution_coord(siz->ysiz, level);
            end += hv_precinct_count(0, w, (uint64_t)1 << ppx) *
                   hv_precinct_count(0, h, (uint64_t)1 << ppy) * siz->csiz;
            while (id < end && hv_cache_complete(cache, HV_BIN_PRECINCT, codestream, id))
                id++;
            if (id == end)
                status->complete++;
        }
    }
    hv_codestream_close(&cs);
    return result;
}
