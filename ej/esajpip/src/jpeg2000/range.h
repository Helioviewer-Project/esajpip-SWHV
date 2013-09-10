#ifndef _JPEG2000_RANGE_H_
#define _JPEG2000_RANGE_H_


#include <iostream>
#include <assert.h>


namespace jpeg2000
{
  using namespace std;


  /**
   * Represents a range of integer values, defined by
   * two values, first and last, which are assumed to
   * be included in the range. Some basic operations
   * are defined for easing the work with ranges.
   */
  class Range
  {
  public:
    int first;	///< First value of the range
    int last;	///< Last value of the range


    /**
     * Initializes the object.
     */
    Range()
    {
      first = 0;
      last = 0;
    }

    /**
     * Initializes the object.
     * @param first First value.
     * @param last Last value.
     */
    Range(int first, int last)
    {
      assert((first >= 0) && (first <= last));

      this->first = first;
      this->last = last;
    }

    /**
     * Copy constructor.
     */
    Range(const Range& range)
    {
      *this = range;
    }

    /**
     * Copy assignment.
     */
    Range& operator=(const Range& range)
    {
      first = range.first;
      last = range.last;

      return *this;
    }

    /**
     * Returns <code>true</code> if the first value if
     * greater or equal to zero, and it is less or
     * equal to the last value.
     */
    bool IsValid() const
    {
      return ((first >= 0) && (first <= last));
    }

    /**
     * Returns an item of the range, starting at the
     * first value.
     * @param i Item index.
     * @return first + i.
     */
    int GetItem(int i) const
    {
      return (first + i);
    }

    /**
     * Returns the index of an item of the range.
     * @param item Item of the range.
     * @return item - first.
     */
    int GetIndex(int item) const
    {
      return (item - first);
    }

    /**
     * Returns the length of the range (last - first + 1).
     */
    int Length() const
    {
      return (last - first + 1);
    }

    friend bool operator==(const Range& a, const Range& b)
    {
       return ((a.first == b.first) && (a.last == b.last));
    }

    friend bool operator!=(const Range& a, const Range& b)
    {
      return ((a.first != b.first) || (a.last != b.last));
    }

    friend ostream& operator << (ostream &out, const Range &range)
    {
      if(range.IsValid()) out << "[" << range.first << " - " << range.last << "]";
      else out << "[ ]";

      return out;
    }

    virtual ~Range()
    {
    }
  };

}


#endif /* _JPEG2000_RANGE_H_ */
