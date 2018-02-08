#ifndef _JPEG2000_META_DATA_H_
#define _JPEG2000_META_DATA_H_

#include <vector>
#include "base.h"
#include "jpeg2000/place_holder.h"

namespace jpeg2000 {
    using namespace data;

    /**
     * Contains the indexing information associated to the
     * meta-data of a JPEG2000 image file. This class can
     * be printed and serialized.
     */
    class Metadata {
    public:
        /**
         * File segments of all the meta-data blocks.
         */
        vector<FileSegment> meta_data;

        /**
         * Associated place-holders.
         */
        vector<PlaceHolder> place_holders;

        /**
         * Empty constructor.
         */
        Metadata() {
        }

        /**
         * Copy constructor.
         */
        Metadata(const Metadata &info) {
            *this = info;
        }

        /**
         * Copy assignment.
         */
        Metadata &operator=(const Metadata &info) {
            base::copy(meta_data, info.meta_data);
            base::copy(place_holders, info.place_holders);

            return *this;
        }

        friend ostream &operator<<(ostream &out, const Metadata &info) {
            out << endl << "Meta-data: ";
            for (size_t i = 0; i < info.meta_data.size(); ++i)
                out << info.meta_data[i] << " ";

            out << endl << "Place Holders: ";
            for (size_t i = 0; i < info.place_holders.size(); ++i)
                out << info.place_holders[i] << " ";

            return out;
        }

        virtual ~Metadata() {
        }
    };
}

#endif /* _JPEG2000_META_DATA_H_ */
