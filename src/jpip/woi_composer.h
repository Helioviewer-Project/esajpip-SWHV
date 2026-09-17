#ifndef _JPIP_WOI_COMPOSER_H_
#define _JPIP_WOI_COMPOSER_H_

#include "woi.h"
#include "jpeg2000/packet.h"
#include "jpeg2000/coding_parameters.h"

namespace jpip {

    /**
     * By means of this class it is possible to find out the
     * which packets of an image are associated to a WOI.
     * Given a WOI and the coding parameters of an image, the
     * code of this class allows to navigate, following the
     * LRCP order, through all the associated packets.
     *
     * @see WOI
     * @see CodingParameters
     */
    class WOIComposer {
    private:
        jpeg2000::Point pxy1;            ///< Upper-left corner of the WOI
        jpeg2000::Point pxy2;            ///< Bottom-right corner of the WOI
        bool more_packets;     ///< Flag to control the last packet
        int max_resolution;    ///< Maximum resolution
        jpeg2000::Size min_precinct_xy;  ///< Minimum precinct
        jpeg2000::Size max_precinct_xy;  ///< Maximum precinct
        jpeg2000::Packet current_packet; ///< Current packet

        void SetResolution(const jpeg2000::CodingParameters *coding_parameters) {
            min_precinct_xy = coding_parameters->GetPrecinctIndex(
                    current_packet.resolution, pxy1);
            max_precinct_xy = coding_parameters->GetPrecinctIndex(
                    current_packet.resolution, pxy2);
            current_packet.precinct_xy = min_precinct_xy;
        }

    public:
        /**
         * Initializes the object. No packets are available.
         */
        WOIComposer() {
            more_packets = false;
            max_resolution = 0;
        }

        /**
         * Resets the packets navigation and starts a new one. Sets the
         * current packet to the first packet of the WOI, assuming a
         * LRCP order.
         * @param coding_parameters Coding parameters to use.
         * @param woi New WOI to use.
         */
        void Reset(const jpeg2000::CodingParameters *coding_parameters, const WOI &woi) {
            more_packets = true;
            current_packet = jpeg2000::Packet();
            max_resolution = woi.resolution;

            pxy1 = woi.position * (1L << (coding_parameters->num_levels - woi.resolution));
            pxy2 = (woi.position + woi.size - 1) * (1L << (coding_parameters->num_levels - woi.resolution));

            SetResolution(coding_parameters);
        }

        /**
         * Returns the current packet.
         */
        const jpeg2000::Packet &GetCurrentPacket() const {
            return current_packet;
        }

        bool HasPacket() const {
            return more_packets;
        }

        /**
         * Moves to the next packet of the WOI.
         * @return <code>true</code> if another packet is available.
         */
        bool GetNextPacket(const jpeg2000::CodingParameters *coding_parameters) {
            if (!more_packets) return false;
            else {
                if (current_packet.precinct_xy.x < max_precinct_xy.x) current_packet.precinct_xy.x++;
                else {
                    current_packet.precinct_xy.x = min_precinct_xy.x;

                    if (current_packet.precinct_xy.y < max_precinct_xy.y) current_packet.precinct_xy.y++;
                    else {
                        current_packet.precinct_xy.y = min_precinct_xy.y;

                        if (current_packet.component < (coding_parameters->num_components - 1))
                            current_packet.component++;
                        else {
                            current_packet.component = 0;

                            if (current_packet.resolution < max_resolution) current_packet.resolution++;
                            else {
                                current_packet.resolution = 0;

                                if (current_packet.layer < (coding_parameters->num_layers - 1)) current_packet.layer++;
                                else {
                                    more_packets = false;
                                    return false;
                                }
                            }

                            SetResolution(coding_parameters);
                        }
                    }
                }
                return true;
            }
        }

    };
}

#endif /* _JPIP_WOI_COMPOSER_H_ */
