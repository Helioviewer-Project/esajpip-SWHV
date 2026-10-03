/* hv_reconstruct.c: see hv_reconstruct.h. */
#include "hv_reconstruct.h"

#include <string.h>

#include "hv_frame.h"
#include "jpeg2000/hv_error.h"

typedef struct {
    uint8_t *out;
    size_t capacity, size;
} output;

static void put(output *o, const void *bytes, size_t n) {
    if (o->size <= o->capacity && n <= o->capacity - o->size)
        memcpy(o->out + o->size, bytes, n);
    o->size += n;
}

size_t hv_reconstruct(const hv_cache *cache, uint64_t codestream, uint8_t *out,
                      size_t capacity, char *error, size_t error_size) {
    static const uint8_t tile_part[] = {
        0xFF, 0x90, 0, 10, 0, 0, 0, 0, 0, 0, 0, 1,     /* SOT: tile 0, to EOC, part 0 of 1 */
        0xFF, 0x93};                                    /* SOD */
    static const uint8_t empty[] = {0x00, 0xFF, 0x92};  /* empty packet, EPH */
    static const uint8_t eoc[] = {0xFF, 0xD9};
    const hv_bin *tile_header = hv_cache_find(cache, HV_BIN_TILE_HEADER, codestream, 0);
    output o = {out, out != NULL ? capacity : 0, 0};
    const hv_frame *frame;
    uint64_t precincts, id, layer;

    frame = hv_frame_get(cache, codestream, error, error_size);
    if (frame == NULL) return 0;
    if (tile_header != NULL && tile_header->length != 0) {
        hv_fail(error, error_size, "tile header data-bin is not empty");
        return 0;
    }
    put(&o, frame->header, frame->header_size);
    precincts = frame->precinct_end[frame->resolutions - 1];

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
            return 0;
        } else {
            for (layer = 0; layer < (uint64_t)frame->layers; layer++)
                put(&o, empty, frame->eph ? 3 : 1);
        }
    }
    put(&o, eoc, sizeof eoc);
    return o.size;
}

int hv_reconstruct_status(const hv_cache *cache, uint64_t codestream, hv_status *status,
                          char *error, size_t error_size) {
    const hv_bin *main_header = hv_cache_find(cache, HV_BIN_MAIN_HEADER, codestream, 0);
    const hv_frame *frame;
    uint64_t id = 0;
    int r;

    memset(status, 0, sizeof *status);
    if (main_header == NULL || !main_header->complete)
        return 0;
    frame = hv_frame_get(cache, codestream, error, error_size);
    if (frame == NULL) return -1;
    status->width = frame->width;
    status->height = frame->height;
    status->components = frame->components;
    status->resolutions = frame->resolutions;
    for (r = 0; r < frame->resolutions; r++) {
        uint64_t end = frame->precinct_end[r];
        while (id < end && hv_cache_complete(cache, HV_BIN_PRECINCT, codestream, id))
            id++;
        if (id != end) break;
        status->complete++;
    }
    return 0;
}
