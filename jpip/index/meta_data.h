#pragma once

#include <vector>
#include "place_holder.h"

namespace jpip {

    /**
     * Contains the indexing information associated to the
     * meta-data of a JPEG2000 image file.
     */
    class Metadata {
    public:
        struct Part {
            FileSegment data;
            PlaceHolder placeholder;

            Part(const FileSegment &_data, const PlaceHolder &_placeholder)
                    : data(_data), placeholder(_placeholder) {
            }
        };

        std::vector<Part> bin0;
        FileSegment tail;

        /**
         * Contents of boxes referenced by place-holders in meta-data bin 0.
         */
        std::vector<FileSegment> bins;

    };
}
