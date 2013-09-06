#ifndef _JPEG2000_INDEX_MANAGER_H_
#define _JPEG2000_INDEX_MANAGER_H_


#include <list>
#include "ipc/mutex.h"
#include "image_index.h"
#include "file_manager.h"


namespace jpeg2000
{
  using namespace ipc;


  /**
   * Manages the indexing information of a repository fo images.
   * Maintains a list in memory of the indexes (using the class
   * <code>ImageIndex</code> for the nodes) of all the opened
   * images and allows a multi-thread access to the information.
   *
   * @see FileManager
   * @see ImageIndex
   */
  class IndexManager
  {
  private:
	/**
	 * Mutex for the operations with the list.
	 */
    Mutex mutex;

    FileManager file_manager_;		///< File manager
    list<ImageIndex> index_list;	///< List of the indexes

    /**
     * Unsafely (without mutex) opens an image and adds its index
     * to the list.
     * @param path_image_file Path of the image file.
     * @param image_index Receives the pointer to the image index created.
     * @return <code>true</code> if successful.
     */
    bool UnsafeOpenImage(string &path_image_file, ImageIndex::Ptr *image_index);

    /**
     * Unsafely (without mutex) closes an image and removes its index
     * from the list, only if it is not used by any other one.
     * @param image_index Associated image index.
     * @return <code>true</code> if successful.
     */
    bool UnsafeCloseImage(const ImageIndex::Ptr& image_index);

  public:
    /**
     * Empty constructor.
     */
    IndexManager()
    {
    }

    /**
     * Initializes the object.
     * @param root_dir Root directory of the image repository.
     * @param cache_dir Directory used for caching.
     * @return <code>true</code> if successful
     */
    bool Init(string root_dir, string cache_dir)
    {
      return file_manager_.Init(root_dir, cache_dir) && mutex.Init(false);
    }

    /**
     * Returns a pointer to the first image index.
     */
    ImageIndex::Ptr GetBegin()
    {
    	return index_list.begin();
    }

    /**
     * Returns a pointer to the last image index.
     */
    ImageIndex::Ptr GetEnd()
    {
    	return index_list.end();
    }

    /**
     * Returns a reference to the base file manager.
     */
    FileManager& file_manager()
    {
      return file_manager_;
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
    bool CloseImage(const ImageIndex::Ptr& image_index);

    /**
     * Returns the size of the list.
     */
    int GetSize() const
    {
      return (int)index_list.size();
    }

    virtual ~IndexManager()
    {
    }
  };
}

#endif /* _JPEG2000_INDEX_MANAGER_H_ */

