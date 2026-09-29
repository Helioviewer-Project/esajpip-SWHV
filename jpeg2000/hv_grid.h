/* Shared nonnegative grid arithmetic (T.800 B-14 and B-16).
 * Resolution levels are 0..32, and precinct_size is positive. */
#ifndef HV_GRID_H
#define HV_GRID_H
#include <stdint.h>

static inline uint64_t hv_resolution_coord(uint64_t value, unsigned level) {
    return (value >> level) + ((value & (((uint64_t)1 << level) - 1)) != 0);
}

/* B-5 to B-10. Validated SIZ coordinates are nonnegative; size is positive. */
static inline uint64_t hv_tile_axis_count(uint64_t end, uint64_t origin, uint64_t size) {
    return (end - origin + size - 1) / size;
}

typedef struct { uint64_t first, end; } hv_interval;

static inline hv_interval hv_tile_axis_extent(uint64_t first, uint64_t end,
                                              uint64_t origin, uint64_t size,
                                              uint64_t index) {
    hv_interval tile = {origin + index * size, origin + (index + 1) * size};
    if (tile.first < first) tile.first = first;
    if (tile.end > end) tile.end = end;
    return tile;
}

/* Number of precinct cells intersecting [first, end). */
static inline uint64_t hv_precinct_count(uint64_t first, uint64_t end,
                                        uint64_t precinct_size) {
    if (end <= first) return 0;
    return end / precinct_size + (end % precinct_size != 0) - first / precinct_size;
}
#endif
