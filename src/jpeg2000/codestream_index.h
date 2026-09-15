#ifndef _JPEG2000_CODESTREAM_INDEX_H_
#define _JPEG2000_CODESTREAM_INDEX_H_

#include <vector>
#include "data/file_segment.h"

namespace jpeg2000 {

    /**
     * Class used for indexing the information of a JPEG2000
     * codestream. The indexed information is the segment of
     * the main header, the contiguous segments of packets
     * (usually the data of each tile-part) and the segments
     * of the existing PLT markers.
     *
     * @see data::FileSegment
     */
    class CodestreamIndex {
    public:
        data::FileSegment header;                    ///< Main header segment
        std::vector<data::FileSegment> packets;        ///< Tile-part packets segments
        std::vector<data::FileSegment> PLT_markers;    ///< PLT markers segments

        /**
         * Empty constructor.
         */
        CodestreamIndex() {
        }

    };
}

#endif /* _JPEG2000_CODESTREAM_INDEX_H_ */
