#ifndef _JPEG2000_INDEX_MANAGER_H_
#define _JPEG2000_INDEX_MANAGER_H_

#include <list>
#include "file_manager.h"
#include "image_index.h"

namespace jpeg2000 {

    /**
     * Manages the indexing information of a repository of images.
     * Maintains a list in memory of the indexes (using the class
     * <code>ImageIndex</code> for the nodes) of all the opened
     * images.
     *
     * @see FileManager
     * @see ImageIndex
     */
    class IndexManager {
    private:
        FileManager file_manager_;      ///< File manager
        list<ImageIndex> index_list;    ///< List of the indexes
        map<const string, File::Ptr> file_map;

    public:
        /**
         * Empty constructor.
         */
        IndexManager() {
        }

        ImageIndex::Ptr GetImage() {
            return --index_list.end();
        }

        /**
         * Initializes the object.
         * @param root_dir Root directory of the image repository.
         * @return <code>true</code> if successful
         */
        bool Init(const string &root_dir) {
            return file_manager_.Init(root_dir);
        }

        /**
         * Opens an image and adds its index to the list.
         * @param path_image_file Path of the image file.
         * @param image_index Receives the pointer to the image index created.
         * @return <code>true</code> if successful.
         */
        bool OpenImage(string &path_image_file);

        /**
         * Closes an image and removes its index
         * from the list, only if it is not used by any other one.
         * @param image_index Associated image index.
         * @return <code>true</code> if successful.
         */
        bool CloseImage();

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
