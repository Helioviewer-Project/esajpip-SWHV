#include "coding_parameters.h"

#include <climits>
#include <cstdint>

namespace jpeg2000 {

    Size CodingParameters::SizeAtLevel(int level) const {
        uint64_t scale = uint64_t(1) << level;
        return Size(DivideRoundUp(size.x, scale),
                    DivideRoundUp(size.y, scale));
    }

    int CodingParameters::GetPositionIndex(int r, int px, int py, int *position_resolutions,
                                           int *resolution_at_position) const {
        int64_t x = (static_cast<int64_t>(px) * resolutions[r].precinct_size.x) << (num_levels - r);
        int64_t y = (static_cast<int64_t>(py) * resolutions[r].precinct_size.y) << (num_levels - r);
        int position_index = 0;
        *position_resolutions = 0;
        *resolution_at_position = 0;

        for (int i = 0; i <= num_levels; ++i) {
            int64_t step_x = static_cast<int64_t>(resolutions[i].precinct_size.x) << (num_levels - i);
            int64_t step_y = static_cast<int64_t>(resolutions[i].precinct_size.y) << (num_levels - i);

            position_index += static_cast<int>((y + step_y - 1) / step_y) *
                              resolutions[i].num_precincts.x;
            if (y % step_y != 0)
                continue;

            position_index += static_cast<int>((x + step_x - 1) / step_x);
            if (x % step_x == 0) {
                if (i < r)
                    (*resolution_at_position)++;
                (*position_resolutions)++;
            }
        }
        return position_index;
    }

    bool CodingParameters::FillPrecinctCounts() {
        total_precincts = 0;
        int64_t total = 0;
        for (int i = 0; i <= num_levels; ++i) {
            Resolution &resolution = resolutions[i];
            resolution.num_precincts = GetPrecinctCount(i, size);
            int64_t count = static_cast<int64_t>(resolution.num_precincts.x) *
                            resolution.num_precincts.y;
            if (count > INT_MAX - total)
                return false;
            resolution.first_precinct = static_cast<int>(total);
            total += count;
        }
        if (num_components <= 0 || num_layers <= 0 ||
            total > INT_MAX / num_components / num_layers)
            return false;
        total_precincts = static_cast<int>(total);
        return true;
    }

    int CodingParameters::GetClosestResolution(const Size &res_size, Size *res_image_size) const {
        int final_level = 0;
        uint64_t requested_area = static_cast<uint64_t>(res_size.x) * res_size.y;
        uint64_t level_area = static_cast<uint64_t>(size.x) * size.y;
        uint64_t minimum = level_area > requested_area
                           ? level_area - requested_area
                           : requested_area - level_area;
        *res_image_size = size;

        for (int level = 1; level <= num_levels; ++level) {
            Size level_size = SizeAtLevel(level);
            level_area = static_cast<uint64_t>(level_size.x) * level_size.y;
            uint64_t distance = level_area > requested_area
                                ? level_area - requested_area
                                : requested_area - level_area;

            if (distance < minimum) {
                *res_image_size = level_size;
                minimum = distance;
                final_level = level;
            }
        }
        return num_levels - final_level;
    }

    int CodingParameters::GetRoundUpResolution(const Size &res_size, Size *res_image_size) const {
        for (int level = num_levels; level >= 0; --level) {
            *res_image_size = SizeAtLevel(level);
            if (res_image_size->x >= res_size.x && res_image_size->y >= res_size.y)
                return num_levels - level;
        }
        *res_image_size = size;
        return num_levels;
    }

    int CodingParameters::GetRoundDownResolution(const Size &res_size, Size *res_image_size) const {
        for (int level = 0; level <= num_levels; ++level) {
            *res_image_size = SizeAtLevel(level);
            if (res_image_size->x <= res_size.x && res_image_size->y <= res_size.y)
                return num_levels - level;
        }
        *res_image_size = SizeAtLevel(num_levels);
        return 0;
    }
}
