#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "jpip/index/point.h"
#include "jpip/jpip.h"

namespace jpip {

    class ResponseRequest {
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
        bool has_stream = false;

    public:
        enum { MAX_CODESTREAM_INDEX = 100000 };

        // Ranges are visited in insertion order. These methods also record
        // which selector is present and the default for unqualified models.
        void AddStream(uint64_t first, uint64_t last, uint64_t step = 1);
        void AddContext(uint64_t first, uint64_t last);

        struct Parameters {
            bool fsiz = false;
            bool roff = false;
            bool rsiz = false;
            bool len = false;
            bool model = false;
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

        Size woi_size;           ///< WOI size
        Point woi_position;      ///< WOI position
        int length_response;     ///< Maximum response length
        Parameters has;          ///< Parameters present in the request
        Size resolution_size;    ///< Size of the resolution level
        std::vector<ModelUpdate> model;

        /**
         * Round direction.
         */
        RoundDirection round_direction;

        /**
         * Empty constructor.
         */
        ResponseRequest() {
            length_response = 0;
            round_direction = ROUNDDOWN;
        }

        bool HasSelections() const { return !codestream_selections.empty(); }

        bool HasWOI() const {
            return has.fsiz || has.roff || has.rsiz;
        }

        bool SelectCodestreams(std::size_t available,
                               std::vector<int> *selected) const;
        bool GetUnqualifiedModelCodestream(std::size_t available,
                                           int *codestream) const;
    };
}
