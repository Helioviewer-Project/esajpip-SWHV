#ifndef _JPEG2000_CODESTREAM_INDEX_H_
#define _JPEG2000_CODESTREAM_INDEX_H_


#include <vector>
#include "base.h"
#include "data/file_segment.h"


namespace jpeg2000
{
  using namespace data;


  /**
   * Class used for indexing the information of a JPEG2000
   * codestream. The indexed information is the segment of
   * the main header, the contiguous segments of packets
   * (usually the data of each tile-part) and the segments
   * of the existing PLT markers. This class can be printed
   * and serialized.
   *
   * @see data::FileSegment
   */
  class CodestreamIndex
  {
  public:
    FileSegment header;					///< Main header segment
    vector<FileSegment> packets;		///< Tile-part packets segments
    vector<FileSegment> PLT_markers;	///< PLT markers segments


    /**
     * Empty constructor.
     */
    CodestreamIndex()
    {
    }

    /**
     * Copy constructor.
     */
    CodestreamIndex(const CodestreamIndex& index)
    {
      *this = index;
    }

    /**
     * Clears the information.
     */
    void Clear()
    {
      packets.clear();
      PLT_markers.clear();
    }

    /**
     * Copy assignment.
     */
    const CodestreamIndex& operator=(const CodestreamIndex& index)
    {
      header = index.header;
      base::copy(packets, index.packets);
      base::copy(PLT_markers, index.PLT_markers);

      return *this;
    }

    template<typename T> T& SerializeWith(T& stream)
    {
      return (stream & header & packets & PLT_markers);
    }

    friend ostream& operator << (ostream &out, const CodestreamIndex &index)
    {
        out << "Header: " << index.header << endl;

        out << "Packets: ";

        for(vector<FileSegment>::const_iterator i = index.packets.begin(); i != index.packets.end(); i++)
            out << *i << " ";

        out << endl << "PLT-markers: ";

        for(vector<FileSegment>::const_iterator i = index.PLT_markers.begin(); i != index.PLT_markers.end(); i++)
            out << *i << " ";

        out << endl;

        return out;
    }

    virtual ~CodestreamIndex()
    {
    }
  };

}


#endif /* _JPEG2000_CODESTREAM_INDEX_H_ */
