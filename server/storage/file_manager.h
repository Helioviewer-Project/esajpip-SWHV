#pragma once

#include <climits>
#include <map>
#include <memory>
#include <utility>

#include "jpip/index/image_index.h"
#include "file.h"

namespace server {

    /**
     * Manages the image files of a repository, allowing read their
     * indexing information, with a caching mechanism for efficiency.
     */
    class FileManager : public jpip::SourceProvider {
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

        std::unique_ptr<jpip::ImageIndex> image;
        std::map<std::string, File> file_map;

        OpenResult ReadImage(const std::string &name_image_file,
                             jpip::ImageIndex *image_index);

    public:
        /**
         * Initializes the object.
         * @param root_dir Root directory of the image repository, ending in '/'.
         * @return <code>true</code> if successful
         */
        bool Init(const std::string &root_dir) {
            if (root_dir.empty())
                return false;
            root_dir_ = root_dir;
            return true;
        }

        jpip::ImageIndex *GetImage() {
            return image.get();
        }

        OpenResult OpenImage(const std::string &path_image_file);

        const jpip::Source *GetSource(const std::string &path_file) override {
            std::map<std::string, File>::const_iterator found =
                    file_map.find(path_file);
            if (found != file_map.end())
                return &found->second;

            File &file = file_map[path_file];
            File::OpenResult opened = file.Open(path_file.c_str(), INT_MAX);
            if (opened != File::OpenResult::OPENED) {
                file_map.erase(path_file);
                if (opened == File::OpenResult::TOO_LARGE)
                    ERROR("Unsupported JPEG 2000 source size in '" << path_file << "'");
                return nullptr;
            }
            return &file;
        }

        void ReleaseSource(const std::string &path) override { file_map.erase(path); }

        void ClearFiles() {
            file_map.clear();
        }

    };
}
