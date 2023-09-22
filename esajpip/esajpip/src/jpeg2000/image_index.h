#ifndef _JPEG2000_INDEX_NODE_H_
#define _JPEG2000_INDEX_NODE_H_

//#define SHOW_TRACES
#include "trace.h"

#include <vector>
#include "image_info.h"
#include "packet_index.h"

namespace jpeg2000 {
    using namespace std;

    class FileManager;

    class ImageIndex {
    private:
        friend class FileManager;

        vector<int> last_plt;
        vector<int> last_packet;
        vector<uint64_t> last_offset_PLT;
        vector<uint64_t> last_offset_packet;

        string path_name;           ///< Image file name
        Metadata meta_data;         ///< Image Metadata
        vector<int> max_resolution; ///< Maximum resolution number

        vector<PacketIndex> packet_indexes;  ///< Code-stream packet index
        vector<CodestreamIndex> codestreams; ///< Image code-streams

        vector<SHARED_PTR<ImageIndex>> hyper_links; ///< Image hyperlinks

        /**
         * Gets the packet lengths from a PLT marker.
         * @param file File where to read the data from.
         * @param ind_codestream Codestream index.
         * @param length_packet It is returned the length of the packet.
         * @return <code>true</code> if successful.
         */
        bool GetPLTLength(File::Ptr &file, int ind_codestream, uint64_t *length_packet);

        /**
         * Gets the packet offsets.
         * @param file File where to read the data from.
         * @param ind_codestream Codestream index.
         * @param length_packet Packet length.
         * @return <code>true</code> if successful.
         */
        void GetOffsetPacket(File::Ptr &file, int ind_codestream, uint64_t length_packet);

        /**
         * Builds the required index for the required resolution levels.
         * @param ind_codestream Codestream index.
         * @param max_index Maximum resolution level.
         * @return <code>true</code> if successful
         */
        bool BuildIndex(FileManager &file_manager, int ind_codestream, int max_index);

        /**
         * Initializes the object.
         * @param path_name Path name of the image.
         * @param image_info Indexing image information.
         */
        void Init(const string &path_name, const ImageInfo &image_info);

        /**
         * Initializes the object.
         * @param path_name Path name of the image.
         * @param image_info Indexing image information.
         * @param index Image index.
         */
        void Init(const string &path_name, const ImageInfo &image_info, int index);

        /**
         * Empty constructor. Only the index manager can
         * use this constructor.
         */
        ImageIndex() {
        }

    public:
        /**
         * Pointer of an object of this class.
         */
        typedef SHARED_PTR<ImageIndex> Ptr;

        /**
         * Returns the number of codestreams.
         */
        size_t GetNumCodestreams() const {
            return codestreams.empty() ? hyper_links.size() : codestreams.size();
        }

        /**
         * Returns the number of meta-data blocks.
         */
        size_t GetNumMetadatas() const {
            return meta_data.meta_data.size();
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
            return codestreams.empty() ? hyper_links[num_codestream]->path_name : path_name;
        }

        /**
         * Returns the file segment the main header of a given
         * codestream.
         * @param num_codestream Codestream number
         */
        const FileSegment &GetMainHeader(int num_codestream) const {
            return codestreams.empty() ? hyper_links[num_codestream]->codestreams.back().header : codestreams[num_codestream].header;
        }

        /**
         * Returns the file segment of a meta-data block.
         * @param num_metadata Meta-data number.
         */
        const FileSegment &GetMetadata(size_t num_metadata) const {
            return meta_data.meta_data[num_metadata];
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
        FileSegment GetPacket(FileManager &file_manager, int num_codestream, const Packet &packet, int *offset = NULL);

        friend ostream &operator<<(ostream &out, const ImageIndex &info_node) {
            out << "Image file name: " << info_node.path_name << endl
                << "Max resolution: ";
            for (size_t i = 0; i < info_node.max_resolution.size(); ++i)
                out << info_node.max_resolution[i] << "  ";
            out << endl;

            for (size_t i = 0; i < info_node.codestreams.size(); ++i)
                out << "Codestream index: " << endl << "----------------- " << endl << info_node.codestreams[i] << endl << endl;
            out << "Packet indexes: " << endl << "--------------- " << endl;
            for (size_t i = 0; i < info_node.packet_indexes.size(); ++i)
                for (int j = 0; j < info_node.packet_indexes[i].Size(); ++j)
                    out << j << " - " << info_node.packet_indexes[i][j] << endl;
            out << endl << "Num. Hyperlinks: " << info_node.hyper_links.size() << endl;
            for (size_t i = 0; i < info_node.hyper_links.size(); ++i)
                out << "Hyperlinks: " << endl << "----------- " << endl << *info_node.hyper_links[i] << endl << "----------- " << endl;

            out << endl << "Meta-data: ";
            out << endl << info_node.meta_data << endl;

            return out;
        }

        virtual ~ImageIndex() {
            TRACE("Destroying the image index of '" << path_name << "'");
        }
    };
}

#endif /* _JPEG2000_INDEX_NODE_H_ */
