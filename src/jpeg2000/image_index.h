#ifndef _JPEG2000_IMAGE_INDEX_H_
#define _JPEG2000_IMAGE_INDEX_H_

//#define SHOW_TRACES
#include "trace.h"

#include <string>
#include <vector>

#include "coding_parameters.h"
#include "data/file.h"
#include "meta_data.h"
#include "packet_index.h"

namespace jpeg2000 {

    class FileManager;

    class ImageIndex {
    private:
        friend class FileManager;

        struct Codestream {
            std::string path;
            CodingParameters parameters;
            data::FileSegment header;
            std::vector<data::FileSegment> packet_data;
            std::vector<data::FileSegment> plt;
            int last_plt;
            int last_packet;
            uint64_t last_offset_PLT;
            uint64_t last_offset_packet;
            PacketIndex packet_index;

            explicit Codestream(const std::string &_path);
        };

        std::string path_name;           ///< Image file name
        Metadata meta_data;         ///< Image Metadata
        std::vector<Codestream> codestreams;

        /**
         * Gets the packet lengths from a PLT marker.
         * @param file File where to read the data from.
         * @param codestream Codestream state.
         * @param length_packet It is returned the length of the packet.
         * @return <code>true</code> if successful.
         */
        static bool GetPLTLength(data::File *file, Codestream &codestream,
                                 uint64_t *length_packet);

        /**
         * Gets the packet offsets.
         * @param codestream Codestream state.
         * @param length_packet Packet length.
         * @return <code>true</code> if successful.
         */
        static bool GetOffsetPacket(Codestream &codestream, uint64_t length_packet);

        /**
         * Builds the packet index through a given progression index.
         * @param file File where to read the PLT marker data from.
         * @param codestream Codestream state.
         * @param max_index Last packet index to build.
         * @return <code>true</code> if successful
         */
        static bool BuildIndex(data::File *file, Codestream &codestream,
                               int max_index);

        explicit ImageIndex(const std::string &_path);

        ImageIndex(const ImageIndex &) = delete;
        ImageIndex &operator=(const ImageIndex &) = delete;

    public:
        /**
         * Returns the number of codestreams.
         */
        size_t GetNumCodestreams() const {
            return codestreams.size();
        }

        const Metadata &GetMetadata() const {
            return meta_data;
        }

        /**
         * Returns the path name of the image.
         */
        const std::string &GetPathName() const {
            return path_name;
        }

        /**
         * Returns the path name of a given codestream, if it is
         * a hyperlinked codestream.
         * @param num_codestream Codestream number.
         */
        const std::string &GetPathName(int num_codestream) const {
            return codestreams[num_codestream].path;
        }

        /**
         * Returns the file segment the main header of a given
         * codestream.
         * @param num_codestream Codestream number
         */
        const data::FileSegment &GetMainHeader(int num_codestream) const {
            return codestreams[num_codestream].header;
        }

        const CodingParameters *GetCodingParameters(int num_codestream) const {
            return &codestreams[num_codestream].parameters;
        }

        /**
         * Returns the file segment of a packet.
         * @param num_codestream Codestream number.
         * @param packet Packet information.
         * @param offset If it is not <code>NULL</code> receives the
         * offset of the packet.
         */
        bool GetPacket(data::File *file, int num_codestream, const Packet &packet,
                       data::FileSegment *segment, int *offset = NULL);

        ~ImageIndex() {
            TRACE("Destroying the image index of '" << path_name << "'");
        }
    };
}

#endif /* _JPEG2000_IMAGE_INDEX_H_ */
