#ifndef _JPEG2000_PLACE_HOLDER_H_
#define _JPEG2000_PLACE_HOLDER_H_

#include <cstdint>

#include "data/file_segment.h"

namespace jpeg2000 {

    /**
     * Contains the information of a place-holder.
     */
    class PlaceHolder {
    public:
        int id;                    ///< Place-holder identifier.
        bool is_jp2c;            ///< <code>true</code> if refers to a codestream.
        data::FileSegment header;        ///< File segment associated to the box header

        /**
         * Initializes the object.
         */
        PlaceHolder() {
            id = 0;
            is_jp2c = false;
        }

        /**
         * Initializes the object.
         * @param id Place-holder identifier.
         * @param is_jp2c Indicates if is a codestream place-holder.
         * @param header File segment of the associated header.
         */
        PlaceHolder(int id, bool is_jp2c, const data::FileSegment &header) {
            this->id = id;
            this->is_jp2c = is_jp2c;
            this->header = header;
        }

        /**
         * Returns the length of the place-holder.
         */
        uint64_t length() const {
            return ((is_jp2c ? 44 : 20) + header.length);
        }

    };
}

#endif /* _JPEG2000_PLACE_HOLDER_H_ */
