//#define SHOW_TRACES

#include "trace.h"
#include "index_manager.h"

namespace jpeg2000 {
    using namespace std;

    bool IndexManager::OpenImage(string &path_image_file) {
        if (path_image_file[0] == '/') path_image_file = path_image_file.substr(1, path_image_file.size() - 1);
        path_image_file = file_manager_.root_dir() + path_image_file;

        // Get image info
        ImageInfo image_info;
        if (!file_manager_.ReadImage(*this, path_image_file, &image_info)) {
            ERROR("The image file '" << path_image_file << "' can not be read");
            return false;
        }
        coding_parameters = new CodingParameters(image_info.coding_parameters);

        image = ImageIndex::Ptr(new ImageIndex());
        image->Init(path_image_file, image_info);

        // Repeat the process with the image hyperlinks
        if (!image_info.paths.empty()) {
            image->hyper_links.resize(image_info.paths.size());
            for (multimap<string, int>::const_iterator i = image_info.paths.begin(); i != image_info.paths.end(); ++i) {
                ImageIndex::Ptr linked = ImageIndex::Ptr(new ImageIndex());
                linked->Init(i->first, image_info, i->second);
                image->hyper_links[i->second] = linked;
            }
        }
        return true;
    }

}
