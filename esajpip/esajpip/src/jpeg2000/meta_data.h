#ifndef _JPEG2000_META_DATA_H_
#define _JPEG2000_META_DATA_H_

#include <vector>
#include "place_holder.h"

namespace jpeg2000 {
    using namespace data;

    /**
     * Contains the indexing information associated to the
     * meta-data of a JPEG2000 image file. This class can
     * be printed and serialized.
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

        vector<Part> bin0;
        FileSegment tail;

        /**
         * Contents of boxes referenced by place-holders in meta-data bin 0.
         */
        vector<FileSegment> bins;

        friend ostream &operator<<(ostream &out, const Metadata &info) {
            out << endl << "Meta-data bin 0: ";
            for (size_t i = 0; i < info.bin0.size(); ++i)
                out << info.bin0[i].data << " " << info.bin0[i].placeholder << " ";
            out << info.tail;

            out << endl << "Meta-data bins: ";
            for (size_t i = 0; i < info.bins.size(); ++i)
                out << info.bins[i] << " ";

            return out;
        }

    };
}

#endif /* _JPEG2000_META_DATA_H_ */
