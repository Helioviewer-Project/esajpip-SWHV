#ifndef _JPEG2000_PACKET_INDEX_H_
#define _JPEG2000_PACKET_INDEX_H_


#include "data/vint_vector.h"
#include "data/file_segment.h"


namespace jpeg2000
{
  using namespace data;


  /**
   * Class used for indexing the packets of a codestream image.
   * The class <code>vint_vector</code> is used internally to
   * store the offsets of the packets with the minimum required
   * bytes.
   *
   * @see data::vint_vector
   */
  class PacketIndex
  {
  private:
	/**
	 * Vector of packet offsets.
	 */
    vint_vector offsets;

    /**
     * Vector of file segments to handle the different
     * sets of packets that are not contiguous.
     */
    vector<FileSegment> aux;

  public:
    enum {
      /**
       * All the offsets must be greater than this value.
       */
      MINIMUM_OFFSET = 64
    };

    /**
     * Empty constructor.
     */
    PacketIndex()
    {
    }

    /**
     * Initializes the object.
     * @param max_offset Maximum value for an offset.
     */
    PacketIndex(uint64_t max_offset)
    {
      assert(max_offset > 0);

      int num_bytes = 0;

      while(max_offset > 0) {
        max_offset >>= 8;
        num_bytes++;
      }
      offsets.set_num_bytes(num_bytes);
    }

    /**
     * Copy constructor.
     */
    PacketIndex(const PacketIndex& index)
    {
      *this = index;
    }

    /**
     * Copy assignment.
     */
    const PacketIndex& operator=(const PacketIndex& index)
    {
      offsets = index.offsets;
      aux.clear();
      for(vector<FileSegment>::const_iterator i = index.aux.begin(); i != index.aux.end(); i++)
        aux.push_back(*i);

      return *this;
    }

    /**
     * Adds a new packet segment to the index.
     * @param segment Fiel segment associated to the packet.
     * @return The object itself.
     */
    PacketIndex& Add(const FileSegment& segment)
    {
      assert(segment.offset >= MINIMUM_OFFSET);

      int last = aux.size() - 1;

      if(last < 0) {
        aux.push_back(segment);
        offsets.push_back(0);

      } else {
        if(aux[last].IsContiguousTo(segment)) {
          offsets.back() = aux[last].offset;
          offsets.push_back(last);
          aux[last] = segment;

        } else {
          assert(last < (MINIMUM_OFFSET - 1));

          offsets.push_back(last + 1);
          aux.push_back(segment);
        }
      }

      return *this;
    }

    /**
     * Returns the number of elements of the vector.
     */
    int Size() const
    {
      return offsets.size();
    }

    /**
     * Clears the content.
     */
    void Clear()
    {
      offsets.clear();
      aux.clear();
    }

    /**
     * Operator used for accessing the items.
     * @param i Item index.
     * @return File segment of the packet.
     */
    FileSegment operator[](int i) const
    {
      uint64_t off_i = offsets[i];

      if(off_i < MINIMUM_OFFSET) return aux[off_i];
      else {
        uint64_t off_i1 = offsets[i + 1];

        if(off_i1 < MINIMUM_OFFSET)
          off_i1 = aux[off_i1].offset;

        return FileSegment(off_i, off_i1 - off_i);
      }
    }

    virtual ~PacketIndex()
    {
    }
  };

}


#endif /* _JPEG2000_PACKET_INDEX_H_ */
