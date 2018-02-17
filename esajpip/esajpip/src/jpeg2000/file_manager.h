#ifndef _JPEG2000_FILE_MANAGER_H_
#define _JPEG2000_FILE_MANAGER_H_

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
        CodingParameters coding_parameters; ///< Image coding parameters

        map<const string, File::Ptr> file_map;

        /**
         * Reads the header information. of a JP2/JPX box.
         * @param fim Image file.
         * @param type_box Receives the type of the box.
         * @param length_box Receives the length of the box.
         * @return <code>true</code> if successful.
         */
        bool ReadBoxHeader(File::Ptr &file, uint32_t *type_box, uint64_t *length_box);

        /**
         * Reads the information of a codestream.
         * @param file Image file.
         * @param params Receives the coding parameters.
         * @param index Receives the indexing information.
         * @return <code>true</code> if successful.
         */
        bool ReadCodestream(File::Ptr &file, CodingParameters *params, CodestreamIndex *index);

        /**
         * Reads the information of a SIZ marker.
         * @param file Image file.
         * @param params Pointer to the coding parameters to update.
         * @return <code>true</code> if successful.
         */
        bool ReadSIZMarker(File::Ptr &file, CodingParameters *params);

        /**
         * Reads the information of a COD marker.
         * @param file Image file.
         * @param params Pointer to the coding parameters to update.
         * @return <code>true</code> if successful.
         */
        bool ReadCODMarker(File::Ptr &file, CodingParameters *params);

        /**
         * Reads the information of a SOT marker.
         * @param file Image file.
         * @param index Pointer to the indexing information to update.
         * @return <code>true</code> if successful.
         */
        bool ReadSOTMarker(File::Ptr &file, CodestreamIndex *index);

        /**
         * Reads the information of a PLT marker.
         * @param file Image file.
         * @param index Pointer to the indexing information to update.
         * @return <code>true</code> if successful.
         */
        bool ReadPLTMarker(File::Ptr &file, CodestreamIndex *index);

        /**
         * Reads the information of a SOD marker.
         * @param file Image file.
         * @param index Pointer to the indexing information to update.
         * @return <code>true</code> if successful.
         */
        bool ReadSODMarker(File::Ptr &file, CodestreamIndex *index);

        /**
         * Reads the information of a NLST box.
         * @param file Image file.
         * @param num_codestream Receives the number of codestream read.
         * @param length_box Box length in bytes.
         * @return <code>true</code> if successful.
         */
        bool ReadNlstBox(File::Ptr &file, int *num_codestream, int length_box);

        /**
         * Reads the information of a FLST box.
         * @param file Image file.
         * @param length_box Box length in bytes.
         * @param data_reference Receives the data reference.
         * @return <code>true</code> if successful.
         */
        bool ReadFlstBox(File::Ptr &file, uint64_t length_box, uint16_t *data_reference);

        /**
         * Reads the information of a URL box.
         * @param file Image file.
         * @param length_box Box length in bytes.
         * @param path_file Receives the URL path read.
         * @return <code>true</code> if successful.
         */
        bool ReadUrlBox(File::Ptr &file, uint64_t length_box, string *path_file);

        /**
         * Reads the information of a JP2 image file.
         * @param file Image file.
         * @param image_info Receives the image information.
         * @return <code>true</code> if successful.
         */
        bool ReadJP2(File::Ptr &file, ImageInfo *image_info);

        /**
         * Reads the information of a JPX image file.
         * @param file Image file.
         * @param image_info Receives the image information.
         * @return <code>true</code> if successful.
         */
        bool ReadJPX(File::Ptr &file, ImageInfo *image_info);

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

        /**
         * Returns a pointer to the coding parameters.
         */
        const CodingParameters *GetCodingParameters() const {
            return &coding_parameters;
        }

        bool OpenImage(const string &image_file);

        File::Ptr GetFile(const string &path_file) {
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

        virtual ~FileManager() {
        }
    };
}

#endif /* _JPEG2000_FILE_MANAGER_H_ */
