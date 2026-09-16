#ifndef _JPIP_REQUEST_H_
#define _JPIP_REQUEST_H_

#include <string>
#include <vector>
#include "woi.h"
#include "cache_model.h"
#include "jpeg2000/point.h"
#include "jpeg2000/coding_parameters.h"

namespace jpip {

    class Request {
    private:
        bool valid;
        void ParseURI(const std::string &uri);

    public:
        std::string object;
        std::string target;
        std::string channel;

        bool Parse(const std::string &line);

        struct Parameters {
            bool fsiz = false;
            bool roff = false;
            bool rsiz = false;
            bool metareq = false;
            bool len = false;
            bool target = false;
            bool cid = false;
            bool cnew = false;
            bool cclose = false;
            bool model = false;
            bool stream = false;
            bool context = false;
        };

        /**
         * Enumeration of the possible round directions
         * of a WOI for specifying the resolution levels.
         */
        enum RoundDirection {
            ROUNDUP,      ///< Round-up
            ROUNDDOWN,    ///< Round-down
            CLOSEST       ///< Closest
        };

        jpeg2000::Size woi_size;           ///< WOI size
        jpeg2000::Point woi_position;      ///< WOI position
        std::vector<int> codestreams; ///< Requested codestreams
        int length_response;     ///< Maximum response length
        Parameters has;          ///< Parameters present in the request
        jpeg2000::Size resolution_size;    ///< Size of the resolution level
        CacheModel cache_model;  ///< Cache model

        /**
         * Round direction.
         */
        RoundDirection round_direction;

        /**
         * Empty constructor.
         */
        Request() {
            object = "/";
            valid = true;
            length_response = 0;
            round_direction = CLOSEST;
            codestreams.reserve(100);
        }

        bool HasWOI() const {
            return has.fsiz || has.roff || has.rsiz;
        }

        /**
         * Obtains the resolution level and modifies the given WOI to adjust it
         * according to that level.
         * @param coding_parameters Associated coding parameters.
         * @param woi WOI to modify.
         * @return Image size at the selected resolution.
         */
        jpeg2000::Size GetResolution(
                const jpeg2000::CodingParameters *coding_parameters, WOI *woi) const {
            jpeg2000::Size res_image_size;

            if (round_direction == Request::CLOSEST)
                woi->resolution = coding_parameters->GetClosestResolution(resolution_size, &res_image_size);
            else if (round_direction == Request::ROUNDUP)
                woi->resolution = coding_parameters->GetRoundUpResolution(resolution_size, &res_image_size);
            else if (round_direction == Request::ROUNDDOWN)
                woi->resolution = coding_parameters->GetRoundDownResolution(resolution_size, &res_image_size);

            if (resolution_size.x > 0 && resolution_size.y > 0 && resolution_size != res_image_size) {
                woi->position.x = (int) ceil((double) woi->position.x * res_image_size.x / resolution_size.x);
                woi->position.y = (int) ceil((double) woi->position.y * res_image_size.y / resolution_size.y);
                woi->size.x = (int) ceil((double) woi->size.x * res_image_size.x / resolution_size.x);
                woi->size.y = (int) ceil((double) woi->size.y * res_image_size.y / resolution_size.y);
            }
            return res_image_size;
        }
    };
}

#endif /* _JPIP_REQUEST_H_ */
