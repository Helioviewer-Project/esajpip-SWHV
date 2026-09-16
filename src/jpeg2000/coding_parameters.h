#ifndef _JPEG2000_CODING_PARAMETERS_H_
#define _JPEG2000_CODING_PARAMETERS_H_

#include <vector>
#include <cmath>

#include "point.h"
#include "trace.h"
#include "packet.h"

namespace jpeg2000 {
    /**
     * Contains the coding parameters of a JPEG2000 image codestream.
     */
    class CodingParameters {
    private:
        int total_precincts;

        /**
         * Returns the index of a packet according to the RPCL progression.
         * @param l Quality layer.
         * @param r Resolution level.
         * @param c Component.
         * @param px Precinct position X.
         * @param py Precinct position Y.
         */
        int GetProgressionIndexRPCL(int l, int r, int c, int px, int py) const {
            Size precinct_point = GetPrecincts(r, size);
            return (resolutions[r].first_precinct * num_components * num_layers) +
                   (py * precinct_point.x * num_components * num_layers) +
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
            Size precinct_point = GetPrecincts(r, size);
            return (resolutions[r].first_precinct * num_components * num_layers) +
                   (l * num_components * precinct_point.x * precinct_point.y) +
                   (c * precinct_point.x * precinct_point.y) + (py * precinct_point.x) + px;
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
            Size precinct_point = GetPrecincts(r, size);
            return (l * total_precincts * num_components) +
                   (num_components * resolutions[r].first_precinct) +
                   (c * precinct_point.x * precinct_point.y) + (py * precinct_point.x) + px;
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
            int first_precinct;

            Resolution(int _width, int _height)
                    : precinct_size(_width, _height), first_precinct(0) {
            }
        };

        Size size;                ///< Image size
        int num_levels;            ///< Number of resolution levels
        int num_layers;            ///< Number of quality layers
        int progression;        ///< Progression order
        int num_components;        ///< Number of components
        bool position_order_supported;

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
            position_order_supported = false;
            total_precincts = 0;
        }

        void FillPrecinctCounts();

        /**
         * Returns <code>true</code> if the progression starts with resolution.
         */
        bool IsResolutionProgression() const {
            return progression == RLCP_PROGRESSION || progression == RPCL_PROGRESSION;
        }

        bool IsLayerLastProgression() const {
            return progression == RPCL_PROGRESSION || progression == PCRL_PROGRESSION ||
                   progression == CPRL_PROGRESSION;
        }

        int GetNumPackets() const {
            return total_precincts * num_components * num_layers;
        }

        int GetNumPrecinctDataBins() const {
            return total_precincts * num_components;
        }

        /**
         * Returns a precinct coordinate adjusted to a given resolution level.
         * @param r Resolution level.
         * @param point Precinct coordinate.
         */
        Size GetPrecincts(int r, const Size &point) const {
            return Size(
                    (int) ceil(ceil((double) point.x / (1L << (num_levels - r))) / (double) resolutions[r].precinct_size.x),
                    (int) ceil(ceil((double) point.y / (1L << (num_levels - r))) / (double) resolutions[r].precinct_size.y)
            );
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
                    ERROR("Progression (" << progression << ") not supported");
            }
            return 0;
        }

        /**
         * Returns the data-bin identifier associated to the
         * given packet.
         * @param packet Packet information.
         */
        int GetPrecinctDataBinId(const Packet &packet) const {
            Size precinct_point = GetPrecincts(packet.resolution, size);
            int s = resolutions[packet.resolution].first_precinct +
                    (precinct_point.x * packet.precinct_xy.y) +
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
