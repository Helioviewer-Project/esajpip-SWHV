#pragma once

#include <climits>

#include "jpip/index/point.h"

namespace jpip {

    /**
     * Class that identifies a WOI (Window Of Interest). This term
     * refers, from the point of view of the JPIP protocol, to a
     * rectangular region of an image, at a resolution level and
     * with a quality-layer limit.
     *
     * @see Point
     */
    class WOI {
    public:
        Size size;        ///< Size of the WOI (width and height)
        Point position;    ///< Position of the upper-left corner of the WOI
        int resolution;    ///< Resolution level where the WOI is located (0 == the lowest)
        int layers = INT_MAX; // Initial quality layers belonging to the window.

        /**
         * Initializes the resolution level to zero.
         */
        WOI() {
            resolution = 0;
        }

        /**
         * Initializes the object.
         * @param position Position of the WOI.
         * @param size Size of the WOI.
         * @param resolution Resolution level of the WOI.
         */
        WOI(const Point &position, const Size &size,
            int resolution) {
            this->size = size;
            this->position = position;
            this->resolution = resolution;
        }

        /**
         * Returns <code>true</code> if the two given WOIs
         * are equal.
         */
        friend bool operator==(const WOI &a, const WOI &b) {
            return a.position == b.position && a.size == b.size &&
                   a.resolution == b.resolution && a.layers == b.layers;
        }

        /**
         * Returns <code>true</code> if the two given WOIs
         * are not equal.
         */
        friend bool operator!=(const WOI &a, const WOI &b) {
            return !(a == b);
        }

    };
}
