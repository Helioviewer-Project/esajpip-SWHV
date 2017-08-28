#ifndef _JPEG2000_INDEX_NODE_H_
#define _JPEG2000_INDEX_NODE_H_

//#define SHOW_TRACES
#include "trace.h"

#include <list>
#include <vector>
#include "base.h"
#include "range.h"
#include "image_info.h"
#include "packet_index.h"
#include "ipc/rdwr_lock.h"

namespace jpeg2000 {
    using namespace std;
    using namespace ipc;

    /**
     * Contains the indexing information of a JPEG2000 image file that
     * is managed by the index manager. This class can be printed.
     *
     * Maintains a read/write lock for controlling the multi-thread
     * access to the indexing information. For instance, by default
     * all the threads usually want to read the information. The
     * packet index built on demand, so only when a thread wants to
     * create a new level of the packet index, it needs to write.
     *
     * @see IndexManager
     */
    class ImageIndex {
    private:
        friend class IndexManager;

        /**
         * Read/write lock.
         */
        RdWrLock::Ptr rdwr_lock;

        vector<int> last_plt;
        vector<int> last_packet;
        vector<uint64_t> last_offset_PLT;
        vector<uint64_t> last_offset_packet;

        string path_name;            ///< Image file name
        Metadata meta_data;          ///< Image Metadata
        int num_references;          ///< Number of references
        vector<int> max_resolution;  ///< Maximum resolution number

        vector<PacketIndex> packet_indexes;    ///< Code-stream packet index
        vector<CodestreamIndex> codestreams;    ///< Image code-streams

        CodingParameters::Ptr coding_parameters;            ///< Image coding parameters
        vector<list<ImageIndex>::iterator> hyper_links;    ///< Image hyperlinks

        /**
         * Gets the packet lengths from a PLT marker.
         * @param file File where to read the data from.
         * @param ind_codestream Codestream index.
         * @param length_packet It is returned the length of the packet.
         * @return <code>true</code> if successful.
         */
        bool GetPLTLength(const File &file, int ind_codestream, uint64_t *length_packet);

        /**
         * Gets the packet offsets.
         * @param file File where to read the data from.
         * @param ind_codestream Codestream index.
         * @param length_packet Packet length.
         * @return <code>true</code> if successful.
         */
        void GetOffsetPacket(const File &file, int ind_codestream, uint64_t length_packet);

        /**
         * Builds the required index for the required resolution levels.
         * @param ind_codestream Codestream index.
         * @param max_index Maximum resolution level.
         * @return <code>true</code> if successful
         */
        bool BuildIndex(int ind_codestream, int max_index);

        /**
         * Initializes the object.
         * @param path_name Path name of the image.
         * @param image_info Indexing image information.
         * @return <code>true</code> if successful
         */
        bool Init(const string &path_name, const ImageInfo &image_info);

        /**
         * Initializes the object.
         * @param path_name Path name of the image.
         * @param coding_parameters Coding parameters.
         * @param image_info Indexing image information.
         * @param index Image index.
         * @return <code>true</code> if successful
         */
        bool Init(const string &path_name, CodingParameters::Ptr coding_parameters,
                  const ImageInfo &image_info, int index);

        /**
         * Empty constructor. Only the index manager can
         * use this constructor.
         */
        ImageIndex() {
            num_references = 0;
        }

    public:
        /**
         * Pointer of an object of this class.
         */
        typedef list<ImageIndex>::iterator Ptr;

        /**
         * Copy constructor.
         */
        ImageIndex(const ImageIndex &image_index) {
            *this = image_index;
        }

        /**
         * Returns the number of codestreams.
         */
/*
        int GetNumCodestreams() const {
            if (codestreams.size() > 0) return codestreams.size();
            else return hyper_links.size();
        }
*/

        /**
         * Returns the number of meta-data blocks.
         */
        size_t GetNumMetadatas() const {
            return meta_data.meta_data.size();
        }

        /**
         * Gets the lock for reading, for a specific range of
         * codestreams.
         * @return <code>true</code> if successful
         */
        bool ReadLock(const Range &range = Range(0, 0));

        /**
         * Releases the lock for reading, for a specific range of
         * codestreams.
         * @return <code>true</code> if successful
         */
        bool ReadUnlock(const Range &range = Range(0, 0));

        /**
         * Returns the path name of the image.
         */
        string GetPathName() const {
            return path_name;
        }

        /**
         * Returns the path name of a given codestream, if it is
         * a hyperlinked codestream.
         * @param num_codestream Codestream number.
         */
        string GetPathName(int num_codestream) const {
            if (codestreams.size() > 0) return path_name;
            else return hyper_links[num_codestream]->path_name;
        }

        /**
         * Returns the file segment the main header of a given
         * codestream.
         * @param num_codestream Codestream number
         */
        FileSegment GetMainHeader(int num_codestream) const {
            if (codestreams.size() > 0) return codestreams[num_codestream].header;
            else return hyper_links[num_codestream]->codestreams.back().header;
        }

        /**
         * Returns the file segment of a meta-data block.
         * @param num_metadata Meta-data number.
         */
        FileSegment GetMetadata(size_t num_metadata) const {
            return meta_data.meta_data[num_metadata];
        }

        /**
         * Returns the information of a place-holder.
         * @param num_placeholder Place-holder number.
         */
        PlaceHolder GetPlaceHolder(int num_placeholder) const {
            return meta_data.place_holders[num_placeholder];
        }

        /**
         * Returns the file segment of a packet.
         * @param num_codestream Codestream number.
         * @param packet Packet information.
         * @param offset If it is not <code>NULL</code> receives the
         * offset of the packet.
         */
        FileSegment GetPacket(int num_codestream, const Packet &packet, int *offset = NULL);

        /**
         * Returns a pointer to the coding parameters.
         */
        CodingParameters::Ptr GetCodingParameters() const {
            return coding_parameters;
        }

        /**
         * Returns <code>true</code> if the image contains
         * hyperlinks.
         */
        bool IsHyperLinked(int num_codestream) const {
            return (num_codestream < (int) hyper_links.size());
        }

        /**
         * Returns a pointer to a hyperlink.
         * @param num_codestream Number of the hyperlink (codestream).
         */
/*
        Ptr GetHyperLink(int num_codestream) const {
            return hyper_links[num_codestream];
        }
*/

        /**
         * Returns the number of hyperlinks.
         */
/*
        int GetNumHyperLinks() const {
            return (int) hyper_links.size();
        }
*/

        operator CodingParameters::Ptr() const {
            return coding_parameters;
        }

        ImageIndex &operator=(const ImageIndex &image_index) {
            rdwr_lock = image_index.rdwr_lock;

            meta_data = image_index.meta_data;
            path_name = image_index.path_name;
            num_references = image_index.num_references;
            coding_parameters = image_index.coding_parameters;

            base::copy(max_resolution, image_index.max_resolution);
            base::copy(last_plt, image_index.last_plt);
            base::copy(last_packet, image_index.last_packet);
            base::copy(codestreams, image_index.codestreams);
            base::copy(hyper_links, image_index.hyper_links);
            base::copy(packet_indexes, image_index.packet_indexes);
            base::copy(last_offset_PLT, image_index.last_offset_PLT);
            base::copy(last_offset_packet, image_index.last_offset_packet);

            return *this;
        }

        friend ostream &operator<<(ostream &out, const ImageIndex &info_node) {
            out << "Image file name: " << info_node.path_name << endl
                << *(info_node.coding_parameters)
                << "Coding parameters ref: " << info_node.coding_parameters.use_count() << endl
                << "Max resolution: ";
            for (vector<int>::const_iterator i = info_node.max_resolution.begin();
                 i != info_node.max_resolution.end(); i++)
                out << *i << "  ";
            out << endl;

            for (vector<CodestreamIndex>::const_iterator i = info_node.codestreams.begin();
                 i != info_node.codestreams.end(); i++)
                out << "Codestream index: " << endl << "----------------- " << endl << *i << endl << endl;

            out << "Packet indexes: " << endl << "--------------- " << endl;
            for (vector<PacketIndex>::const_iterator i = info_node.packet_indexes.begin();
                 i != info_node.packet_indexes.end(); i++)
                for (int j = 0; j < i->Size(); j++)
                    out << j << " - " << (*i)[j] << endl;

            out << endl << "Num. Hyperlinks: " << info_node.hyper_links.size() << endl;
            for (vector<list<ImageIndex>::iterator>::const_iterator i = info_node.hyper_links.begin();
                 i != info_node.hyper_links.end(); i++)
                out << "Hyperlinks: " << endl << "----------- " << endl << **i << endl << "----------- " << endl;

            out << endl << "Meta-data: ";
            out << endl << info_node.meta_data;
            out << endl << "Num. References: " << info_node.num_references << endl;

            return out;
        }

        virtual ~ImageIndex() {
            TRACE("Destroying the image index of '" << path_name << "'");
        }
    };
}

#endif /* _JPEG2000_INDEX_NODE_H_ */
