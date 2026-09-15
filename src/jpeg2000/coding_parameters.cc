#include "coding_parameters.h"

#include <cstdint>

namespace jpeg2000 {

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
            Size precincts = GetPrecincts(i, size);

            position_index += static_cast<int>((y + step_y - 1) / step_y) * precincts.x;
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
            resolutions[i].first_precinct = total_precincts;
            Size precincts = GetPrecincts(i, size);
            total_precincts += precincts.x * precincts.y;
        }
    }

    int CodingParameters::GetClosestResolution(const Size &res_size, Size *res_image_size) const {
        int distance, final_r = 0;
        int distance_x = size.x - res_size.x;
        int distance_y = size.y - res_size.y;
        int min = abs(distance_x) + abs(distance_y);
        int res_image_x, res_image_y;
        res_image_size->x = size.x;
        res_image_size->y = size.y;

        for (int r = 1; r <= num_levels; ++r) {
            res_image_x = (int) ceil((double) size.x / (1L << r));
            res_image_y = (int) ceil((double) size.y / (1L << r));
            distance_x = res_image_x - res_size.x;
            distance_y = res_image_y - res_size.y;
            distance = abs(distance_x) + abs(distance_y);

            if (distance < min) {
                res_image_size->x = res_image_x;
                res_image_size->y = res_image_y;
                min = distance;
                final_r = r;
            }
        }

        int res = (num_levels - final_r);

        if (res > num_levels) res = num_levels;
        else if (res < 0) res = 0;

        return res;
    }

    int CodingParameters::GetRoundUpResolution(const Size &res_size, Size *res_image_size) const {
        int r = num_levels;
        bool bigger = false;

        while (!bigger && r >= 0) {
            res_image_size->x = (int) ceil((double) size.x / (1L << r));
            res_image_size->y = (int) ceil((double) size.y / (1L << r));
            if ((res_image_size->x >= res_size.x) &&
                (res_image_size->y >= res_size.y))
                bigger = true;
            else r--;
        }

        int res = (num_levels - r);

        if (res > num_levels) res = num_levels;
        else if (res < 0) res = 0;

        return res;
    }

    int CodingParameters::GetRoundDownResolution(const Size &res_size, Size *res_image_size) const {
        int r = 0;
        bool smaller = false;

        while (!smaller && r <= num_levels) {
            res_image_size->x = (int) ceil((double) size.x / (1L << r));
            res_image_size->y = (int) ceil((double) size.y / (1L << r));
            if ((res_image_size->x <= res_size.x) &&
                (res_image_size->y <= res_size.y))
                smaller = true;
            else r++;
        }

        int res = (num_levels - r);

        if (res > num_levels) res = num_levels;
        else if (res < 0) res = 0;

        return res;
    }
}
