#ifndef _JPEG2000_PACKET_INDEX_H_
#define _JPEG2000_PACKET_INDEX_H_

#include <vector>
#include "data/file_segment.h"

namespace jpeg2000 {
    using namespace data;

    /**
     * Class used for indexing the packets of a codestream image.
     */
    class PacketIndex {
    private:
        /**
         * Vector of packet offsets.
         */
        vector<uint32_t> offsets;

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
        PacketIndex() = default;

        /**
         * Initializes the object.
         * @param max_offset Maximum value for an offset.
         */
        explicit PacketIndex(uint64_t max_offset) {
            assert(max_offset <= UINT32_MAX);
        }

        /**
         * Adds a new packet segment to the index.
         * @param segment File segment associated to the packet.
         * @return The object itself.
         */
        PacketIndex &Add(const FileSegment &segment) {
            assert(segment.offset >= MINIMUM_OFFSET);

            int last = aux.size() - 1;

            if (last < 0) {
                aux.push_back(segment);
                offsets.push_back(0);
            } else {
                if (aux[last].IsContiguousTo(segment)) {
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
        int Size() const {
            return offsets.size();
        }

        /**
         * Operator used for accessing the items.
         * @param i Item index.
         * @return File segment of the packet.
         */
        bool Get(int i, FileSegment *segment) const {
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

                *segment = FileSegment(off_i, off_i1 - off_i);
                return true;
            }
        }

        FileSegment operator[](int i) const {
            FileSegment segment;
            Get(i, &segment);
            return segment;
        }

    };
}

#endif /* _JPEG2000_PACKET_INDEX_H_ */
