#ifndef _JPEG2000_FILE_MANAGER_H_
#define _JPEG2000_FILE_MANAGER_H_

#include <map>
#include <memory>
#include <utility>

#include "image_index.h"

namespace jpeg2000 {

    /**
     * Manages the image files of a repository, allowing read their
     * indexing information, with a caching mechanism for efficiency.
     */
    class FileManager {
    private:
        std::string root_dir_;    ///< Root directory of the repository

        std::unique_ptr<ImageIndex> image;
        std::map<std::string, std::unique_ptr<data::File>> file_map;

        /**
         * Reads the header information. of a JP2/JPX box.
         * @param fim Image file.
         * @param type_box Receives the type of the box.
         * @param length_box Receives the length of the box.
         * @return <code>true</code> if successful.
         */
        bool ReadBoxHeader(data::File *file, uint64_t limit, uint32_t *type_box,
                           uint64_t *length_box);

        /**
         * Reads the information of a codestream.
         * @param file Image file.
         * @param params Receives the coding parameters.
         * @param index Receives the indexing information.
         * @return <code>true</code> if successful.
         */
        bool ReadCodestream(data::File *file, uint64_t length, CodingParameters *params,
                            CodestreamIndex *index);

        /**
         * Reads the information of a SIZ marker.
         * @param file Image file.
         * @param params Pointer to the coding parameters to update.
         * @return <code>true</code> if successful.
         */
        bool ReadSIZMarker(data::File *file, uint64_t limit, CodingParameters *params);

        /**
         * Reads the information of a COD marker.
         * @param file Image file.
         * @param params Pointer to the coding parameters to update.
         * @return <code>true</code> if successful.
         */
        bool ReadCODMarker(data::File *file, uint64_t limit, CodingParameters *params);

        /**
         * Reads the information of a SOT marker.
         * @param file Image file.
         * @param index Pointer to the indexing information to update.
         * @return <code>true</code> if successful.
         */
        bool ReadSOTMarker(data::File *file, uint64_t limit, CodestreamIndex *index,
                           uint8_t *declared_tile_parts);

        /**
         * Reads the information of a PLT marker.
         * @param file Image file.
         * @param index Pointer to the indexing information to update.
         * @return <code>true</code> if successful.
         */
        bool ReadPLTMarker(data::File *file, uint64_t limit, CodestreamIndex *index);

        /**
         * Reads the information of a SOD marker.
         * @param file Image file.
         * @param index Pointer to the indexing information to update.
         * @return <code>true</code> if successful.
         */
        bool ReadSODMarker(data::File *file, uint64_t limit, CodestreamIndex *index);

        /**
         * Reads the information of a FLST box.
         * @param file Image file.
         * @param length_box Box length in bytes.
         * @param fragment Receives the fragment location.
         * @param data_reference Receives the data reference.
         * @return <code>true</code> if successful.
         */
        bool ReadFlstBox(data::File *file, uint64_t length_box,
                         data::FileSegment *fragment, uint16_t *data_reference);

        /**
         * Reads the information of a URL box.
         * @param file Image file.
         * @param length_box Box length in bytes.
         * @param path_file Receives the URL path read.
         * @return <code>true</code> if successful.
         */
        bool ReadUrlBox(data::File *file, uint64_t length_box, std::string *path_file);

        /**
         * Reads the information of a JP2 image file.
         * @param file Image file.
         * @param image_index Receives the image information.
         * @return <code>true</code> if successful.
         */
        bool ReadJP2(data::File *file, ImageIndex *image_index);

        /**
         * Reads the information of a JPX image file.
         * @param file Image file.
         * @param image_index Receives the image information.
         * @return <code>true</code> if successful.
         */
        bool ReadJPX(data::File *file, ImageIndex *image_index);

        /**
         * Reads an image file and creates the associated cache file if
         * it does not exist yet.
         * @param name_image_file File name of the image.
         * @param image_index Receives the information of the image.
         * @return <code>true</code> if successful.
         */
        bool ReadImage(const std::string &name_image_file, ImageIndex *image_index);

    public:
        /**
         * Initializes the object.
         * @param root_dir Root directory of the image repository.
         * @return <code>true</code> if successful
         */
        bool Init(const std::string &root_dir) {
            if (root_dir.empty())
                return false;
            root_dir_ = root_dir;
            if (root_dir_.back() != '/')
                root_dir_ += '/';
            return true;
        }

        ImageIndex *GetImage() {
            return image.get();
        }

        bool OpenImage(std::string &path_image_file);

        data::File *GetFile(const std::string &path_file) {
            std::map<std::string, std::unique_ptr<data::File>>::const_iterator found =
                    file_map.find(path_file);
            if (found != file_map.end())
                return found->second.get();

            std::unique_ptr<data::File> file(new data::File());
            if (!file->Open(path_file))
                return nullptr;
            data::File *result = file.get();
            file_map.emplace(path_file, std::move(file));
            return result;
        }

        void ClearFiles() {
            file_map.clear();
        }

    };
}

#endif /* _JPEG2000_FILE_MANAGER_H_ */
