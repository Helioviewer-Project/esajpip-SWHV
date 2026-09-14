#ifndef _JPEG2000_POINT_H_
#define _JPEG2000_POINT_H_

namespace jpeg2000 {
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
         * Returns the subtraction of a point with an integer value.
         * The value is subtracted from the two values of the point.
         */
        friend Point operator-(const Point &a, int value) {
            return Point(a.x - value, a.y - value);
        }

        /**
         * Returns the multiplication of a point with an integer value.
         * The value is multiplied to the two values of the point.
         */
        friend Point operator*(const Point &a, int value) {
            return Point(a.x * value, a.y * value);
        }

        /**
         * Returns the sum of two points. The operation is
         * applied each value of each point.
         */
        friend Point operator+(const Point &a, const Point &b) {
            return Point(a.x + b.x, a.y + b.y);
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

#endif /* _JPEG2000_POINT_H_ */
