#pragma once

namespace jpip {
    /**
     * Represents a couple of integer values that can
     * be used to identify a coordinate as well as a
     * size.
     */
    class Point {
    public:
        int x;    ///< Value X
        int y;    ///< Value Y

        /**
         * Initializes the object.
         */
        Point() {
            x = y = 0;
        }

        /**
         * Initializes the object.
         * @param x Value X.
         * @param y Value Y.
         */
        Point(int x, int y) {
            this->x = x;
            this->y = y;
        }

        /**
         * Returns <code>true</code> if the two points are equal.
         */
        friend bool operator==(const Point &a, const Point &b) {
            return ((a.x == b.x) && (a.y == b.y));
        }

        /**
         * Returns <code>true</code> if the two points are not equal.
         */
        friend bool operator!=(const Point &a, const Point &b) {
            return ((a.x != b.x) || (a.y != b.y));
        }

    };

    /**
     * It is a synonymous of the class <code>Point</code>.
     *
     * @see Point
     */
    typedef Point Size;
}
