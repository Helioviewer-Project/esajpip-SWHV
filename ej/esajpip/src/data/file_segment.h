#ifndef _DATA_FILE_SEGMENT_H_
#define _DATA_FILE_SEGMENT_H_


#include <iostream>
#include <stdint.h>
#include <assert.h>


namespace data
{
  using namespace std;


  /**
   * Identifies a data segment of a file. This segment is defined by an offset
   * and a length (number of bytes), both of them with an unsigned integer of
   * 64 bits. This class is serializable and can be printed.
   */
  class FileSegment
  {
  public:
    uint64_t offset;	///< Offset of the data segment.
    uint64_t length;	///< Length of the data segment.


    /**
     * Identifies a null segment, with the offset as well as the length
     * set to zero.
     */
    static const FileSegment Null;


    /**
     * Initializes all the member variables with zero, being a
     * null segment.
     */
    FileSegment()
    {
      offset = length = 0;
    }

    /**
     * Initializes the segment with the given parameters.
     * @param offset Offset of the segment.
     * @param length Length of the segment.
     */
    FileSegment(uint64_t offset, uint64_t length)
    {
      this->offset = offset;
      this->length = length;
    }

    /**
     * Copy constructor.
     */
    FileSegment(const FileSegment& segment)
    {
      *this = segment;
    }

    /**
     * Copy assignment.
     */
    FileSegment& operator=(const FileSegment& segment)
    {
      offset = segment.offset;
      length = segment.length;

      return *this;
    }

    /**
     * Removes the first bytes of the segment. Modifies the segment as
     * if a number of bytes (specified by the parameter) was removed
     * from the beginning of the segment.
     * @param count Number of bytes to remove.
     * @return <code>*this</code>.
     */
    FileSegment& RemoveFirst(int count)
    {
      assert((length - count) >= 0);

      offset += count;
      length -= count;

      return *this;
    }

    /**
     * Removes the last bytes of the segment. Modifies the segment as
     * if a number of bytes (specified by the parameter) was removed
     * from the end of the segment.
     * @param count Number of bytes to remove.
     * @return <code>*this</code>.
     */
    FileSegment& RemoveLast(int count)
    {
      assert((length - count) >= 0);

      length -= count;

      return *this;
    }

    /**
     * Returns <code>true</code> if the segment is contiguous to
     * another given segment, so the first byte of the given segment
     * is just the next byte after the last byte of the segment.
     */
    bool IsContiguousTo(const FileSegment& segment) const
    {
      return ((offset + length) == segment.offset);
    }

    bool operator==(const FileSegment& segment) const
    {
      return ((offset == segment.offset) && (length == segment.length));
    }

    bool operator!=(const FileSegment& segment) const
    {
      return ((offset != segment.offset) || (length != segment.length));
    }

    template<typename T> T& SerializeWith(T& stream)
    {
      return (stream & offset & length);
    }

    friend ostream& operator << (ostream &out, const FileSegment &segment)
    {
        out << "[" << segment.offset << ":" << segment.length << "]";
        return out;
    }

    virtual ~FileSegment()
    {
    }
  };

}



#endif /* _DATA_FILE_SEGMENT_H_ */
