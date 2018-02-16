#ifndef _JPEG2000_INDEX_MANAGER_H_
#define _JPEG2000_INDEX_MANAGER_H_

#include "file_manager.h"
#include "image_index.h"

namespace jpeg2000 {

    class IndexManager {
    private:
        FileManager file_manager_;                 ///< File manager
        ImageIndex::Ptr image;
        const CodingParameters *coding_parameters; ///< Image coding parameters

        map<const string, File::Ptr> file_map;

    public:
        /**
         * Empty constructor.
         */
        IndexManager() {
        }

        const ImageIndex::Ptr GetImage() {
            return image;
        }

        /**
         * Returns a pointer to the coding parameters.
         */
        const CodingParameters *GetCodingParameters() {
            return coding_parameters;
        }

        /**
         * Initializes the object.
         * @param root_dir Root directory of the image repository.
         * @return <code>true</code> if successful
         */
        bool Init(const string &root_dir) {
            return file_manager_.Init(root_dir);
        }

        bool OpenImage(string &path_image_file);

        File::Ptr OpenFile(const string &path_file) {
            try {
                return file_map.at(path_file);
            } catch (...) {
                File::Ptr file = File::Ptr(new File());
                if (!file->Open(path_file))
                    return File::Ptr();
                file_map.insert(pair<const string, File::Ptr>(path_file, file));
                return file;
            }
        }

        virtual ~IndexManager() {
        }
    };
}

#endif /* _JPEG2000_INDEX_MANAGER_H_ */
