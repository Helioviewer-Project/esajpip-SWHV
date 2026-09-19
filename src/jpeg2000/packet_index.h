#ifndef _JPEG2000_PACKET_INDEX_H_
#define _JPEG2000_PACKET_INDEX_H_

#include <limits>
#include <vector>

#include "data/file_segment.h"

namespace jpeg2000 {

    /**
     * Class used for indexing the packets of a codestream image.
     */
    class PacketIndex {
    private:
        /**
         * Vector of packet offsets.
         */
        std::vector<uint32_t> offsets;

        /**
         * Vector of file segments to handle the different
         * sets of packets that are not contiguous.
         */
        std::vector<data::FileSegment> aux;

    public:
        enum {
            MAX_SEGMENTS = 64,
            // Smaller values are reserved for indexes into aux.
            MINIMUM_OFFSET = MAX_SEGMENTS
        };

        /**
         * Adds a new packet segment to the index.
         * @param segment File segment associated to the packet.
         * @return Whether the segment can be represented by the index.
         */
        bool Add(const data::FileSegment &segment) {
            if (segment.offset < MINIMUM_OFFSET ||
                segment.offset > std::numeric_limits<uint32_t>::max())
                return false;

            if (aux.empty()) {
                aux.push_back(segment);
                offsets.push_back(0);
            } else {
                std::size_t last = aux.size() - 1;
                if (aux[last].IsContiguousTo(segment)) {
                    offsets.back() = aux[last].offset;
                    offsets.push_back(static_cast<uint32_t>(last));
                    aux[last] = segment;
                } else {
                    if (aux.size() >= MAX_SEGMENTS)
                        return false;
                    offsets.push_back(static_cast<uint32_t>(aux.size()));
                    aux.push_back(segment);
                }
            }

            return true;
        }

        /**
         * Returns the number of elements of the vector.
         */
        int Size() const {
            return offsets.size();
        }

        /**
         * Operator used for accessing the items.
         * @param i Item index.
         * @return File segment of the packet.
         */
        bool Get(int i, data::FileSegment *segment) const {
            if (i < 0 || i >= (int) offsets.size())
                return false;

            uint64_t off_i = offsets[i];

            if (off_i < MINIMUM_OFFSET) {
                if (off_i >= aux.size())
                    return false;

                *segment = aux[off_i];
                return true;
            }
            else {
                if (i + 1 >= (int) offsets.size())
                    return false;

                uint64_t off_i1 = offsets[i + 1];

                if (off_i1 < MINIMUM_OFFSET) {
                    if (off_i1 >= aux.size())
                        return false;

                    off_i1 = aux[off_i1].offset;
                }

                if (off_i1 < off_i)
                    return false;

                *segment = data::FileSegment(off_i, off_i1 - off_i);
                return true;
            }
        }

    };
}

#endif /* _JPEG2000_PACKET_INDEX_H_ */
