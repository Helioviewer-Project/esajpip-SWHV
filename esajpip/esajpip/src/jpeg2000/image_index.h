#ifndef _JPEG2000_INDEX_NODE_H_
#define _JPEG2000_INDEX_NODE_H_

//#define SHOW_TRACES
#include "trace.h"

#include <ostream>
#include <string>
#include <vector>

#include "coding_parameters.h"
#include "codestream_index.h"
#include "data/file.h"
#include "meta_data.h"
#include "packet_index.h"

namespace jpeg2000 {
    using namespace std;

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
            string path_name;
            CodingParameters coding_parameters;
            Stream stream;

            Link(string &&_path, CodingParameters &&_params,
                 CodestreamIndex &&_codestream);
        };

        string path_name;           ///< Image file name
        Metadata meta_data;         ///< Image Metadata
        CodingParameters coding_parameters; ///< Coding parameters
        vector<Stream> streams;

        vector<Link> hyper_links; ///< Image hyperlinks

        /**
         * Gets the packet lengths from a PLT marker.
         * @param file File where to read the data from.
         * @param stream Codestream index.
         * @param length_packet It is returned the length of the packet.
         * @return <code>true</code> if successful.
         */
        static bool GetPLTLength(File *file, Stream &stream, uint64_t *length_packet);

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
        static bool BuildIndex(File *file, Stream &stream, const CodingParameters &coding_parameters, int r);

        explicit ImageIndex(const string &_path);

        ImageIndex(const ImageIndex &) = delete;
        ImageIndex &operator=(const ImageIndex &) = delete;

    public:
        /**
         * Returns the number of codestreams.
         */
        size_t GetNumCodestreams() const {
            return streams.empty() ? hyper_links.size() : streams.size();
        }

        /**
         * Returns the number of meta-data blocks.
         */
        size_t GetNumMetadatas() const {
            return meta_data.meta_data.size();
        }

        size_t GetNumMetadataBins() const {
            return meta_data.bins.size();
        }

        /**
         * Returns the path name of the image.
         */
        const string &GetPathName() const {
            return path_name;
        }

        /**
         * Returns the path name of a given codestream, if it is
         * a hyperlinked codestream.
         * @param num_codestream Codestream number.
         */
        const string &GetPathName(int num_codestream) const {
            return streams.empty() ? hyper_links[num_codestream].path_name : path_name;
        }

        /**
         * Returns the file segment the main header of a given
         * codestream.
         * @param num_codestream Codestream number
         */
        const FileSegment &GetMainHeader(int num_codestream) const {
            return streams.empty() ? hyper_links[num_codestream].stream.codestream.header : streams[num_codestream].codestream.header;
        }

        const CodingParameters *GetCodingParameters(int num_codestream) const {
            return streams.empty() ? &hyper_links[num_codestream].coding_parameters : &coding_parameters;
        }

        /**
         * Returns the file segment of a meta-data block.
         * @param num_metadata Meta-data number.
         */
        const FileSegment &GetMetadata(size_t num_metadata) const {
            return meta_data.meta_data[num_metadata];
        }

        const FileSegment &GetMetadataBin(size_t num_metadata) const {
            return meta_data.bins[num_metadata];
        }

        /**
         * Returns the information of a place-holder.
         * @param num_placeholder Place-holder number.
         */
        const PlaceHolder &GetPlaceHolder(size_t num_placeholder) const {
            return meta_data.place_holders[num_placeholder];
        }

        /**
         * Returns the file segment of a packet.
         * @param num_codestream Codestream number.
         * @param packet Packet information.
         * @param offset If it is not <code>NULL</code> receives the
         * offset of the packet.
         */
        bool GetPacket(File *file, int num_codestream, const Packet &packet, FileSegment *segment, int *offset = NULL);

        friend ostream &operator<<(ostream &out, const ImageIndex &info_node) {
            out << "Image file name: " << info_node.path_name << endl
                << "Max resolution: ";
            for (size_t i = 0; i < info_node.streams.size(); ++i)
                out << info_node.streams[i].max_resolution << "  ";
            out << endl;

            for (size_t i = 0; i < info_node.streams.size(); ++i)
                out << "Codestream index: " << endl << "----------------- " << endl << info_node.streams[i].codestream << endl << endl;
            out << "Packet indexes: " << endl << "--------------- " << endl;
            for (size_t i = 0; i < info_node.streams.size(); ++i)
                for (int j = 0; j < info_node.streams[i].packet_index.Size(); ++j)
                    out << j << " - " << info_node.streams[i].packet_index[j] << endl;
            out << endl << "Num. Hyperlinks: " << info_node.hyper_links.size() << endl;
            for (size_t i = 0; i < info_node.hyper_links.size(); ++i) {
                const Link &link = info_node.hyper_links[i];
                out << "Hyperlink: " << endl << "----------- " << endl
                    << "Image file name: " << link.path_name << endl
                    << "Max resolution: " << link.stream.max_resolution << endl
                    << "Codestream index: " << endl << "----------------- " << endl
                    << link.stream.codestream << endl << endl
                    << "Packet index: " << endl << "------------- " << endl;
                for (int j = 0; j < link.stream.packet_index.Size(); ++j)
                    out << j << " - " << link.stream.packet_index[j] << endl;
                out << "----------- " << endl;
            }

            out << endl << "Meta-data: ";
            out << endl << info_node.meta_data << endl;

            return out;
        }

        ~ImageIndex() {
            TRACE("Destroying the image index of '" << path_name << "'");
        }
    };
}

#endif /* _JPEG2000_INDEX_NODE_H_ */
