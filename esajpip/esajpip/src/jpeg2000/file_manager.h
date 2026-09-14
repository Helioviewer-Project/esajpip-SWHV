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
        string root_dir_;    ///< Root directory of the repository

        ImageIndex::Ptr image;
        map<string, unique_ptr<File>> file_map;

        /**
         * Reads the header information. of a JP2/JPX box.
         * @param fim Image file.
         * @param type_box Receives the type of the box.
         * @param length_box Receives the length of the box.
         * @return <code>true</code> if successful.
         */
        bool ReadBoxHeader(File *file, uint32_t *type_box, uint64_t *length_box);

        /**
         * Reads the information of a codestream.
         * @param file Image file.
         * @param params Receives the coding parameters.
         * @param index Receives the indexing information.
         * @return <code>true</code> if successful.
         */
        bool ReadCodestream(File *file, CodingParameters *params, CodestreamIndex *index);

        /**
         * Reads the information of a SIZ marker.
         * @param file Image file.
         * @param params Pointer to the coding parameters to update.
         * @return <code>true</code> if successful.
         */
        bool ReadSIZMarker(File *file, CodingParameters *params);

        /**
         * Reads the information of a COD marker.
         * @param file Image file.
         * @param params Pointer to the coding parameters to update.
         * @return <code>true</code> if successful.
         */
        bool ReadCODMarker(File *file, CodingParameters *params);

        /**
         * Reads the information of a SOT marker.
         * @param file Image file.
         * @param index Pointer to the indexing information to update.
         * @return <code>true</code> if successful.
         */
        bool ReadSOTMarker(File *file, CodestreamIndex *index);

        /**
         * Reads the information of a PLT marker.
         * @param file Image file.
         * @param index Pointer to the indexing information to update.
         * @return <code>true</code> if successful.
         */
        bool ReadPLTMarker(File *file, CodestreamIndex *index);

        /**
         * Reads the information of a SOD marker.
         * @param file Image file.
         * @param index Pointer to the indexing information to update.
         * @return <code>true</code> if successful.
         */
        bool ReadSODMarker(File *file, CodestreamIndex *index);

        /**
         * Reads the information of a FLST box.
         * @param file Image file.
         * @param length_box Box length in bytes.
         * @param data_reference Receives the data reference.
         * @return <code>true</code> if successful.
         */
        bool ReadFlstBox(File *file, uint64_t length_box, uint16_t *data_reference);

        /**
         * Reads the information of a URL box.
         * @param file Image file.
         * @param length_box Box length in bytes.
         * @param path_file Receives the URL path read.
         * @return <code>true</code> if successful.
         */
        bool ReadUrlBox(File *file, uint64_t length_box, string *path_file);

        /**
         * Reads the information of a JP2 image file.
         * @param file Image file.
         * @param image_info Receives the image information.
         * @return <code>true</code> if successful.
         */
        bool ReadJP2(File *file, ImageInfo *image_info);

        /**
         * Reads the information of a JPX image file.
         * @param file Image file.
         * @param image_info Receives the image information.
         * @return <code>true</code> if successful.
         */
        bool ReadJPX(File *file, ImageInfo *image_info);

        /**
         * Reads an image file and creates the associated cache file if
         * it does not exist yet.
         * @param name_image_file File name of the image.
         * @param image_info Receives the information of the image.
         * @return <code>true</code> if successful.
         */
        bool ReadImage(const string &name_image_file, ImageInfo *image_info);

    public:
        /**
         * Initializes the object.
         */
        FileManager() {
        }

        /**
         * Initializes the object.
         * @param root_dir Root directory of the image repository.
         * @return <code>true</code> if successful
         */
        bool Init(const string &root_dir) {
            if (root_dir.empty()) return false;
            else {
                if (root_dir.at(root_dir.size() - 1) == '/')
                    root_dir_ = root_dir;
                else
                    root_dir_ = root_dir + '/';
                return true;
            }
        }

        /**
         * Returns the root directory of the image repository.
         */
        const string &root_dir() const {
            return root_dir_;
        }

        const ImageIndex::Ptr GetImage() const {
            return image;
        }

        bool OpenImage(string &path_image_file);

        File *GetFile(const string &path_file) {
            map<string, unique_ptr<File>>::const_iterator found = file_map.find(path_file);
            if (found != file_map.end())
                return found->second.get();

            unique_ptr<File> file(new File());
            if (!file->Open(path_file))
                return nullptr;
            File *result = file.get();
            file_map.emplace(path_file, std::move(file));
            return result;
        }

        void ClearFiles() {
            file_map.clear();
        }

        virtual ~FileManager() {
        }
    };
}

#endif /* _JPEG2000_FILE_MANAGER_H_ */
