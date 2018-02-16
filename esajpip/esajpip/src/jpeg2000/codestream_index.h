#ifndef _JPEG2000_CODESTREAM_INDEX_H_
#define _JPEG2000_CODESTREAM_INDEX_H_

#include <vector>
#include "data/file_segment.h"

namespace jpeg2000 {
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
    class CodestreamIndex {
    public:
        FileSegment header;                    ///< Main header segment
        vector<FileSegment> packets;        ///< Tile-part packets segments
        vector<FileSegment> PLT_markers;    ///< PLT markers segments

        /**
         * Empty constructor.
         */
        CodestreamIndex() {
        }

        /**
         * Copy constructor.
         */
        CodestreamIndex(const CodestreamIndex &index) {
            *this = index;
        }

        /**
         * Copy assignment.
         */
        const CodestreamIndex &operator=(const CodestreamIndex &index) {
            header = index.header;
            packets = index.packets;
            PLT_markers = index.PLT_markers;
            return *this;
        }

        friend ostream &operator<<(ostream &out, const CodestreamIndex &index) {
            out << "Header: " << index.header << endl;
            out << "Packets: ";
            for (size_t i = 0; i < index.packets.size(); ++i)
                out << index.packets[i] << " ";
            out << endl << "PLT-markers: ";
            for (size_t i = 0; i < index.PLT_markers.size(); ++i)
                out << index.PLT_markers[i] << " ";
            out << endl;

            return out;
        }

        virtual ~CodestreamIndex() {
        }
    };
}

#endif /* _JPEG2000_CODESTREAM_INDEX_H_ */
