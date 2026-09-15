#ifndef _JPEG2000_INDEX_NODE_H_
#define _JPEG2000_INDEX_NODE_H_

//#define SHOW_TRACES
#include "trace.h"

#include <string>
#include <vector>

#include "coding_parameters.h"
#include "codestream_index.h"
#include "data/file.h"
#include "meta_data.h"
#include "packet_index.h"

namespace jpeg2000 {

    class FileManager;

    class ImageIndex {
    private:
        friend class FileManager;

        struct Stream {
            int last_plt;
            int last_packet;
            uint64_t last_offset_PLT;
            uint64_t last_offset_packet;
            int max_resolution;
            PacketIndex packet_index;
            CodestreamIndex codestream;

            explicit Stream(CodestreamIndex &&_codestream);
        };

        struct Link {
            std::string path_name;
            CodingParameters coding_parameters;
            Stream stream;

            Link(std::string &&_path, CodingParameters &&_params,
                 CodestreamIndex &&_codestream);
        };

        std::string path_name;           ///< Image file name
        Metadata meta_data;         ///< Image Metadata
        CodingParameters coding_parameters; ///< Coding parameters
        std::vector<Stream> streams;

        std::vector<Link> hyper_links; ///< Image hyperlinks

        /**
         * Gets the packet lengths from a PLT marker.
         * @param file File where to read the data from.
         * @param stream Codestream index.
         * @param length_packet It is returned the length of the packet.
         * @return <code>true</code> if successful.
         */
        static bool GetPLTLength(data::File *file, Stream &stream, uint64_t *length_packet);

        /**
         * Gets the packet offsets.
         * @param file File where to read the data from.
         * @param stream Codestream index.
         * @param length_packet Packet length.
         * @return <code>true</code> if successful.
         */
        static bool GetOffsetPacket(Stream &stream, uint64_t length_packet);

        /**
         * Builds the required index for the required resolution levels.
         * @param stream Codestream index.
         * @param r Maximum resolution level.
         * @return <code>true</code> if successful
         */
        static bool BuildIndex(data::File *file, Stream &stream,
                               const CodingParameters &coding_parameters, int r);

        explicit ImageIndex(const std::string &_path);

        ImageIndex(const ImageIndex &) = delete;
        ImageIndex &operator=(const ImageIndex &) = delete;

    public:
        /**
         * Returns the number of codestreams.
         */
        size_t GetNumCodestreams() const {
            return streams.empty() ? hyper_links.size() : streams.size();
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
            return streams.empty() ? hyper_links[num_codestream].path_name : path_name;
        }

        /**
         * Returns the file segment the main header of a given
         * codestream.
         * @param num_codestream Codestream number
         */
        const data::FileSegment &GetMainHeader(int num_codestream) const {
            return streams.empty() ? hyper_links[num_codestream].stream.codestream.header : streams[num_codestream].codestream.header;
        }

        const CodingParameters *GetCodingParameters(int num_codestream) const {
            return streams.empty() ? &hyper_links[num_codestream].coding_parameters : &coding_parameters;
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

#endif /* _JPEG2000_INDEX_NODE_H_ */
