#ifndef _JPEG2000_PACKET_H_
#define _JPEG2000_PACKET_H_

#include <ostream>

#include "point.h"

namespace jpeg2000 {
    /**
     * Contains the information of a packet. This
     * class can be printed.
     */
    class Packet {
    public:
        int layer;            ///< Quality layer.
        int component;        ///< Component number.
        int resolution;        ///< Resolution level.
        Point precinct_xy;    ///< Precinct coordinate.

        /**
         * Initializes the object to zero.
         */
        Packet() {
            layer = resolution = component = 0;
        }

        /**
         * Initializes the object.
         */
        Packet(int layer, int resolution, int component, Point precinct_xy) {
            this->layer = layer;
            this->resolution = resolution;
            this->component = component;
            this->precinct_xy = precinct_xy;
        }

        friend ostream &operator<<(ostream &out, const Packet &packet) {
            out << packet.layer << "\t" << packet.resolution << "\t" << packet.component << "\t"
                << packet.precinct_xy.y << "\t" << packet.precinct_xy.x;

            return out;
        }
    };
}

#endif /* _JPEG2000_PACKET_H_ */
