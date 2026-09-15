#ifndef _JPEG2000_META_DATA_H_
#define _JPEG2000_META_DATA_H_

#include <vector>
#include "place_holder.h"

namespace jpeg2000 {

    /**
     * Contains the indexing information associated to the
     * meta-data of a JPEG2000 image file.
     */
    class Metadata {
    public:
        struct Part {
            data::FileSegment data;
            PlaceHolder placeholder;

            Part(const data::FileSegment &_data, const PlaceHolder &_placeholder)
                    : data(_data), placeholder(_placeholder) {
            }
        };

        std::vector<Part> bin0;
        data::FileSegment tail;

        /**
         * Contents of boxes referenced by place-holders in meta-data bin 0.
         */
        std::vector<data::FileSegment> bins;

    };
}

#endif /* _JPEG2000_META_DATA_H_ */
