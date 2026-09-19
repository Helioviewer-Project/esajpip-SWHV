#ifndef _JPEG2000_CODING_PARAMETERS_H_
#define _JPEG2000_CODING_PARAMETERS_H_

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "packet.h"
#include "point.h"

namespace jpeg2000 {
    /**
     * Contains the coding parameters of a JPEG2000 image codestream.
     */
    class CodingParameters {
    private:
        int total_precincts;

        static int DivideRoundUp(uint64_t value, uint64_t divisor) {
            return static_cast<int>((value + divisor - 1) / divisor);
        }

        Size SizeAtLevel(int level) const;

        /**
         * Returns the index of a packet according to the RPCL progression.
         * @param l Quality layer.
         * @param r Resolution level.
         * @param c Component.
         * @param px Precinct position X.
         * @param py Precinct position Y.
         */
        int GetProgressionIndexRPCL(int l, int r, int c, int px, int py) const {
            const Size &precincts = resolutions[r].num_precincts;
            return (resolutions[r].first_precinct * num_components * num_layers) +
                   (py * precincts.x * num_components * num_layers) +
                   (px * num_components * num_layers) + (c * num_layers) + l;
        }

        /**
         * Returns the index of a packet according to the RLCP progression.
         * @param l Quality layer.
         * @param r Resolution level.
         * @param c Component.
         * @param px Precinct position X.
         * @param py Precinct position Y.
         */
        int GetProgressionIndexRLCP(int l, int r, int c, int px, int py) const {
            const Size &precincts = resolutions[r].num_precincts;
            return (resolutions[r].first_precinct * num_components * num_layers) +
                   (l * num_components * precincts.x * precincts.y) +
                   (c * precincts.x * precincts.y) + (py * precincts.x) + px;
        }

        /**
         * Returns the index of a packet according to the LRCP progression.
         * @param l Quality layer.
         * @param r Resolution level.
         * @param c Component.
         * @param px Precinct position X.
         * @param py Precinct position Y.
         */
        int GetProgressionIndexLRCP(int l, int r, int c, int px, int py) const {
            const Size &precincts = resolutions[r].num_precincts;
            return (l * total_precincts * num_components) +
                   (num_components * resolutions[r].first_precinct) +
                   (c * precincts.x * precincts.y) + (py * precincts.x) + px;
        }

        int GetPositionIndex(int r, int px, int py, int *position_resolutions,
                             int *resolution_at_position) const;

        int GetProgressionIndexPCRL(int l, int r, int c, int px, int py) const {
            int position_resolutions;
            int resolution_at_position;
            int position_index = GetPositionIndex(r, px, py, &position_resolutions,
                                                  &resolution_at_position);
            return ((position_index * num_components) +
                    (c * position_resolutions) + resolution_at_position) * num_layers + l;
        }

        int GetProgressionIndexCPRL(int l, int r, int c, int px, int py) const {
            int position_resolutions;
            int resolution_at_position;
            int position_index = GetPositionIndex(r, px, py, &position_resolutions,
                                                  &resolution_at_position);
            return ((c * total_precincts) + position_index + resolution_at_position) * num_layers + l;
        }

    public:
        struct Resolution {
            Size precinct_size;
            Size num_precincts;
            int first_precinct;

            Resolution(int _width, int _height)
                    : precinct_size(_width, _height), first_precinct(0) {
            }
        };

        Size size;                ///< Image size
        // Keep rejecting nonzero origins until all precinct and progression
        // geometry is made origin-aware, not only resolution sizing.
        Point origin;
        int num_levels;            ///< Number of resolution levels
        int num_layers;            ///< Number of quality layers
        int progression;        ///< Progression order
        int num_components;        ///< Number of components

        /**
         * Precinct sizes of each resolution level.
         */
        std::vector<Resolution> resolutions;

        /**
         * All the progression orders defined in the JPEG2000
         * standard (Part 1).
         */
        enum {
            LRCP_PROGRESSION = 0,        ///< LRCP
            RLCP_PROGRESSION = 1,        ///< RLCP
            RPCL_PROGRESSION = 2,        ///< RPCL
            PCRL_PROGRESSION = 3,        ///< PCRL
            CPRL_PROGRESSION = 4        ///< CPRL
        };

        /**
         * Initializes the object.
         */
        CodingParameters() {
            num_levels = 0;
            num_layers = 0;
            progression = 0;
            num_components = 0;
            total_precincts = 0;
        }

        bool FillPrecinctCounts();

        int GetNumPrecinctDataBins() const {
            return total_precincts * num_components;
        }

        int GetNumPackets() const {
            return GetNumPrecinctDataBins() * num_layers;
        }

        /**
         * Returns the number of precincts covering an image at a resolution level.
         * @param r Resolution level.
         * @param image_size Full-resolution image size.
         */
        Size GetPrecinctCount(int r, const Size &image_size) const {
            uint64_t scale = uint64_t(1) << (num_levels - r);
            uint64_t width = scale * resolutions[r].precinct_size.x;
            uint64_t height = scale * resolutions[r].precinct_size.y;
            return Size(DivideRoundUp(image_size.x, width),
                        DivideRoundUp(image_size.y, height));
        }

        Size GetPrecinctIndex(int r, int point_resolution,
                              const Point &point) const {
            uint64_t scale = uint64_t(1) << (point_resolution - r);
            uint64_t width = scale * resolutions[r].precinct_size.x;
            uint64_t height = scale * resolutions[r].precinct_size.y;
            return Size(static_cast<int>(static_cast<uint64_t>(point.x) / width),
                        static_cast<int>(static_cast<uint64_t>(point.y) / height));
        }

        /**
         * Returns the index of a packet according to the
         * progression order.
         * @param packet Packet information.
         */
        int GetProgressionIndex(const Packet &packet) const {
            switch (progression) {
                case LRCP_PROGRESSION:
                    return GetProgressionIndexLRCP(packet.layer, packet.resolution, packet.component, packet.precinct_xy.x, packet.precinct_xy.y);
                case RLCP_PROGRESSION:
                    return GetProgressionIndexRLCP(packet.layer, packet.resolution, packet.component, packet.precinct_xy.x, packet.precinct_xy.y);
                case RPCL_PROGRESSION:
                    return GetProgressionIndexRPCL(packet.layer, packet.resolution, packet.component, packet.precinct_xy.x, packet.precinct_xy.y);
                case PCRL_PROGRESSION:
                    return GetProgressionIndexPCRL(packet.layer, packet.resolution, packet.component, packet.precinct_xy.x, packet.precinct_xy.y);
                case CPRL_PROGRESSION:
                    return GetProgressionIndexCPRL(packet.layer, packet.resolution, packet.component, packet.precinct_xy.x, packet.precinct_xy.y);
                default:
                    throw std::logic_error("Invalid JPEG 2000 progression order");
            }
        }

        /**
         * Returns the data-bin identifier associated to the
         * given packet.
         * @param packet Packet information.
         */
        int GetPrecinctDataBinId(const Packet &packet) const {
            const Size &precincts = resolutions[packet.resolution].num_precincts;
            int s = resolutions[packet.resolution].first_precinct +
                    (precincts.x * packet.precinct_xy.y) +
                    packet.precinct_xy.x;
            return (packet.component + (s * num_components));
        }

        /**
         * Returns the resolution level according to the given size and
         * the closest round policy.
         * @param res_size Resolution size.
         * @param res_image_size Image size associated to the
         * resolution level returned.
         */
        int GetClosestResolution(const Size &res_size, Size *res_image_size) const;

        /**
         * Returns the resolution level according to the given size and
         * the round-up round policy.
         * @param res_size Resolution size.
         * @param res_image_size Image size associated to the
         * resolution level returned.
         */
        int GetRoundUpResolution(const Size &res_size, Size *res_image_size) const;

        /**
         * Returns the resolution level according to the given size and
         * the round-down round policy.
         * @param res_size Resolution size.
         * @param res_image_size Image size associated to the
         * resolution level returned.
         */
        int GetRoundDownResolution(const Size &res_size, Size *res_image_size) const;

    };
}

#endif /* _JPEG2000_CODING_PARAMETERS_H_ */
