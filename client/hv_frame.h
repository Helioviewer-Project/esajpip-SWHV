#ifndef HV_FRAME_H
#define HV_FRAME_H

#include "hv_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Prepared from a complete main-header bin, owned by that bin until cache
 * release. Resolution indices run from lowest to highest. */
typedef struct hv_frame {
    uint32_t width, height;
    int components, resolutions, layers, eph;
    uint64_t precinct_end[33];
    uint8_t *header;
    size_t header_size;
} hv_frame;

const hv_frame *hv_frame_get(const hv_cache *cache, uint64_t codestream,
                             char *error, size_t error_size);
void hv_frame_free(hv_frame *frame);

#ifdef __cplusplus
}
#endif
#endif
