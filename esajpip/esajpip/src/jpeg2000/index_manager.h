#ifndef _JPEG2000_INDEX_MANAGER_H_
#define _JPEG2000_INDEX_MANAGER_H_

#include <list>
#include "image_index.h"
#include "file_manager.h"

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

    public:
        /**
         * Empty constructor.
         */
        IndexManager() {
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
         * Returns a pointer to the first image index.
         */
        ImageIndex::Ptr GetBegin() {
            return index_list.begin();
        }

        /**
         * Returns a pointer to the last image index.
         */
        ImageIndex::Ptr GetEnd() {
            return index_list.end();
        }

        /**
         * Opens an image and adds its index to the list.
         * @param path_image_file Path of the image file.
         * @param image_index Receives the pointer to the image index created.
         * @return <code>true</code> if successful.
         */
        bool OpenImage(string &path_image_file, ImageIndex::Ptr *image_index);

        /**
         * Closes an image and removes its index
         * from the list, only if it is not used by any other one.
         * @param image_index Associated image index.
         * @return <code>true</code> if successful.
         */
        bool CloseImage(ImageIndex::Ptr &image_index);

        /**
         * Returns the size of the list.
         */
        int GetSize() const {
            return (int) index_list.size();
        }

        virtual ~IndexManager() {
        }
    };
}

#endif /* _JPEG2000_INDEX_MANAGER_H_ */
