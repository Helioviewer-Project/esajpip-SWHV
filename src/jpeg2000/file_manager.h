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
    public:
        enum class OpenResult {
            OPENED,
            NOT_FOUND,
            INVALID_PATH,
            UNSUPPORTED,
            UNREADABLE,
            INVALID
        };

    private:
        std::string root_dir_;    ///< Root directory of the repository

        std::unique_ptr<ImageIndex> image;
        std::map<std::string, std::unique_ptr<data::File>> file_map;

        static bool ReadCodestream(data::File *file, uint64_t length,
                                   ImageIndex::Codestream &codestream);

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
         * Reads and validates an image file.
         * @param name_image_file File name of the image.
         * @param image_index Receives the information of the image.
         * @return File-open and validation result.
         */
        OpenResult ReadImage(const std::string &name_image_file,
                             ImageIndex *image_index);

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

        OpenResult OpenImage(const std::string &path_image_file);

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
