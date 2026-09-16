#include "coding_parameters.h"

#include <cstdint>
#include <cstdlib>

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

    void CodingParameters::FillPrecinctCounts() {
        total_precincts = 0;
        for (int i = 0; i <= num_levels; ++i) {
            Resolution &resolution = resolutions[i];
            resolution.first_precinct = total_precincts;
            resolution.num_precincts = GetPrecincts(i, size);
            total_precincts += resolution.num_precincts.x * resolution.num_precincts.y;
        }
    }

    int CodingParameters::GetClosestResolution(const Size &res_size, Size *res_image_size) const {
        int final_level = 0;
        int64_t distance_x = static_cast<int64_t>(size.x) - res_size.x;
        int64_t distance_y = static_cast<int64_t>(size.y) - res_size.y;
        int64_t minimum = std::abs(distance_x) + std::abs(distance_y);
        *res_image_size = size;

        for (int level = 1; level <= num_levels; ++level) {
            Size level_size = SizeAtLevel(level);
            distance_x = static_cast<int64_t>(level_size.x) - res_size.x;
            distance_y = static_cast<int64_t>(level_size.y) - res_size.y;
            int64_t distance = std::abs(distance_x) + std::abs(distance_y);

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
