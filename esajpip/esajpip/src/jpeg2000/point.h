#ifndef _JPEG2000_POINT_H_
#define _JPEG2000_POINT_H_


#include <iostream>


namespace jpeg2000
{
  using namespace std;


  /**
   * Represents a couple of integer values that can
   * be used to identify a coordinate as well as a
   * size. This class can be printed and serialized.
   */
  class Point
  {
  public:
    int x;	///< Value X
    int y;	///< Value Y

    /**
     * Initializes the object.
     */
    Point()
    {
      x = y = 0;
    }

    /**
     * Initializes the object.
     * @param x Value X.
     * @param y Value Y.
     */
    Point(int x, int y)
    {
      this->x = x;
      this->y = y;
    }

    /**
     * Copy constructor.
     */
    Point(const Point& p)
    {
      *this = p;
    }

    /**
     * Copy assignment.
     */
    Point& operator=(const Point& p)
    {
      x = p.x;
      y = p.y;

      return *this;
    }

    /**
     * Increments by one the two values.
     * @return The object itself.
     */
    Point& operator++()
    {
      x++;
      y++;

      return *this;
    }

    /**
     * Decrements by one the two values.
     * @return The object itself.
     */
    Point& operator--()
    {
      x--;
      y--;

      return *this;
    }

    /**
     * Increments the two values.
     * @param val Value to increment.
     * @return The object itself.
     */
    Point& operator+=(int val)
    {
      x += val;
      y += val;

      return *this;
    }

    /**
     * Decrements the two values.
     * @param val Value to decrement.
     * @return The object itself.
     */
    Point& operator-=(int val)
    {
      x -= val;
      y -= val;

      return *this;
    }

    /**
     * Multiplies the two values by one value.
     * @param val Value to multiply.
     * @return The object itself.
     */
    Point& operator*=(int val)
    {
      x *= val;
      y *= val;

      return *this;
    }

    /**
     * Divides the two values by one value.
     * @param val Value to divide.
     * @return The object itself.
     */
    Point& operator/=(int val)
    {
      x /= val;
      y /= val;

      return *this;
    }

    /**
     * Returns the sum of a point with an integer value.
     * The value is added to the two values of the point.
     */
    friend Point operator+(const Point& a, int value)
    {
       return Point(a.x + value, a.y + value);
    }

    /**
     * Returns the subtraction of a point with an integer value.
     * The value is subtracted from the two values of the point.
     */
    friend Point operator-(const Point& a, int value)
    {
       return Point(a.x - value, a.y - value);
    }

    /**
     * Returns the multiplication of a point with an integer value.
     * The value is multiplied to the two values of the point.
     */
    friend Point operator*(const Point& a, int value)
    {
       return Point(a.x * value, a.y * value);
    }

    /**
     * Returns the division of a point with an integer value.
     * The value is divided to the two values of the point.
     */
    friend Point operator/(const Point& a, int value)
    {
       return Point(a.x / value, a.y / value);
    }

    /**
     * Returns the sum of two points. The operation is
     * applied each value of each point.
     */
    friend Point operator+(const Point& a, const Point& b)
    {
       return Point(a.x + b.x, a.y + b.y);
    }

    /**
     * Returns the subtraction of two points. The operation is
     * applied each value of each point.
     */
    friend Point operator-(const Point& a, const Point& b)
    {
       return Point(a.x - b.x, a.y - b.y);
    }

    /**
     * Returns the multiplication of two points. The operation is
     * applied each value of each point.
     */
    friend Point operator*(const Point& a, const Point& b)
    {
       return Point(a.x * b.x, a.y * b.y);
    }

    /**
     * Returns the division of two points. The operation is
     * applied each value of each point.
     */
    friend Point operator/(const Point& a, const Point& b)
    {
       return Point(a.x / b.x, a.y / b.y);
    }

    /**
     * Returns <code>true</code> if the two points are equal.
     */
    friend bool operator==(const Point& a, const Point& b)
    {
       return ((a.x == b.x) && (a.y == b.y));
    }

    /**
     * Returns <code>true</code> if the two points are not equal.
     */
    friend bool operator!=(const Point& a, const Point& b)
    {
      return ((a.x != b.x) || (a.y != b.y));
    }

    friend ostream& operator << (ostream &out, const Point &point)
    {
        out << "(" << point.x << ", " << point.y << ")";
        return out;
    }

    template<typename T> T& SerializeWith(T& stream)
    {
      return (stream & x & y);
    }

    virtual ~Point()
    {
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
