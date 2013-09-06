#ifndef _JPIP_WOI_H_
#define _JPIP_WOI_H_


#include <iostream>
#include "jpeg2000/point.h"


namespace jpip
{

  using namespace std;
  using namespace jpeg2000;


  /**
   * Class that identifies a WOI (Window Of Interest). This term
   * refers, from the point of view of the JPIP protocol, to a
   * rectangular region of an image, for a resolution level. This
   * class can be printed.
   *
   * @see Point
   */
  class WOI
  {
  public:
    Size size;		///< Size of the WOI (width and height)
    Point position;	///< Position of the upper-left corner of the WOI
    int resolution;	///< Resolution level where the WOI is located (0 == the highest)


    /**
     * Initializes the resolution level to zero.
     */
    WOI()
    {
      resolution = 0;
    }

    /**
     * Initializes the object.
     * @param position Position of the WOI.
     * @param size Size of the WOI.
     * @param resolution Resolution level of the WOI.
     */
    WOI(const Point& position, const Size& size, int resolution)
    {
      this->size = size;
      this->position = position;
      this->resolution = resolution;
    }

    /**
     * Copy constructor.
     */
    WOI(const WOI& woi)
    {
      *this = woi;
    }

    /**
     * Copy assignment.
     */
    WOI& operator=(const WOI& woi)
    {
      this->size = woi.size;
      this->position = woi.position;
      this->resolution = woi.resolution;

      return *this;
    }

    /**
     * Returns <code>true</code> if the two given WOIs
     * are equal.
     */
    friend bool operator==(const WOI& a, const WOI& b)
    {
       return ((a.position == b.position) && (a.size == b.size) && (a.resolution == b.resolution));
    }

    /**
     * Returns <code>true</code> if the two given WOIs
     * are not equal.
     */
    friend bool operator!=(const WOI& a, const WOI& b)
    {
      return !(a == b);
    }

    friend ostream& operator << (ostream &out, const WOI &woi)
    {
        out << "(" << woi.position.x << ", " << woi.position.y << ", "
            << woi.size.x << ", " << woi.size.y << ", " << woi.resolution << ")";

        return out;
    }

    virtual ~WOI()
    {
    }
  };

}


#endif /* _JPIP_WOI_H_ */
