#ifndef _JPIP_REQUEST_H_
#define _JPIP_REQUEST_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "woi.h"
#include "jpip.h"
#include "jpeg2000/point.h"
#include "jpeg2000/coding_parameters.h"

namespace jpip {

    class Request {
    private:
        struct CodestreamSelection {
            uint64_t first;
            uint64_t last;
            uint64_t step;

            CodestreamSelection(uint64_t _first, uint64_t _last,
                                uint64_t _step)
                    : first(_first), last(_last), step(_step) {
            }
        };

        std::vector<CodestreamSelection> codestream_selections;
        uint64_t first_requested_codestream = 0;

        bool ParseURI(const std::string &uri, std::string *error_message);
        bool ParseStream(const std::string &value);
        void AddContext(int first, int last);

    public:
        std::string object;
        std::string target;
        std::string channel;
        bool accepts_http = false;

        bool Parse(const std::string &line,
                   std::string *error_message = NULL);

        struct Parameters {
            bool fsiz = false;
            bool roff = false;
            bool rsiz = false;
            bool metareq = false;
            bool len = false;
            bool target = false;
            bool tid = false;
            bool cid = false;
            bool cnew = false;
            bool cclose = false;
            bool model = false;
            bool stream = false;
            bool context = false;
        };

        struct ModelUpdate {
            DataBinClass bin_class;
            bool has_codestream_qualifier;
            int first_codestream;
            int last_codestream;
            int id;
            int amount;
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
        int length_response;     ///< Maximum response length
        Parameters has;          ///< Parameters present in the request
        jpeg2000::Size resolution_size;    ///< Size of the resolution level
        std::vector<ModelUpdate> model;

        /**
         * Round direction.
         */
        RoundDirection round_direction;

        /**
         * Empty constructor.
         */
        Request() {
            object = "/";
            length_response = 0;
            round_direction = ROUNDDOWN;
        }

        bool HasWOI() const {
            return has.fsiz || has.roff || has.rsiz;
        }

        bool SelectCodestreams(std::size_t available,
                               std::vector<int> *selected) const;
        bool GetUnqualifiedModelCodestream(std::size_t available,
                                           int *codestream) const;

        /**
         * Obtains the resolution level and modifies the given WOI to adjust it
         * according to that level.
         * @param coding_parameters Associated coding parameters.
         * @param woi WOI to modify.
         * @return Image size at the selected resolution.
         */
        jpeg2000::Size GetResolution(
                const jpeg2000::CodingParameters *coding_parameters, WOI *woi) const;
    };
}

#endif /* _JPIP_REQUEST_H_ */
