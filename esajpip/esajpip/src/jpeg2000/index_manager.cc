//#define SHOW_TRACES

#include "trace.h"
#include "index_manager.h"

namespace jpeg2000 {
    using namespace std;

    bool IndexManager::OpenImage(string &path_image_file, ImageIndex::Ptr *image_index) {
        if (path_image_file[0] == '/') path_image_file = path_image_file.substr(1, path_image_file.size() - 1);
        path_image_file = file_manager_.root_dir() + path_image_file;

        // Look for the image in the list
        for (*image_index = index_list.begin(); *image_index != index_list.end(); ++(*image_index)) {
            if ((*image_index)->path_name == path_image_file) {
                (*image_index)->num_references++;
                return true;
            }
        }
        // Get image info
        ImageInfo image_info;
        if (!file_manager_.ReadImage(path_image_file, &image_info)) {
            ERROR("The image file '" << path_image_file << "' can not be read");
            return false;
        }

        // IndexNode is created
        ImageIndex index_node;
        index_node.Init(path_image_file, image_info);

        // Repeat the process with the image hyperlinks
        if (!image_info.paths.empty()) {
            index_node.hyper_links.resize(image_info.paths.size());
            map<string, int> hyperlinks_visited;
            map<string, int> hyperlinks_created;
            multimap<string, int>::const_iterator it_find;
            pair<multimap<string, int>::iterator, multimap<string, int>::iterator> it_find_range;

            // Increase the number of references of the hyperlinks in the index list
            for (list<ImageIndex>::iterator i = index_list.begin(); i != index_list.end(); ++i) {
                it_find_range = image_info.paths.equal_range(i->path_name);
                for (it_find = it_find_range.first; it_find != it_find_range.second; ++it_find) {
                    i->num_references++;
                    hyperlinks_visited.insert(*it_find);
                    index_node.hyper_links[(*it_find).second] = i;
                }
            }

            // Add the rest of hyperlinks to the index list
            for (multimap<string, int>::const_iterator i = image_info.paths.begin(); i != image_info.paths.end(); ++i) {
                if (hyperlinks_visited.find(i->first) == hyperlinks_visited.end()) {
                    if (hyperlinks_created.find(i->first) == hyperlinks_created.end()) {
                        ImageIndex index_node_linked;
                        index_node_linked.Init(i->first, index_node.coding_parameters, image_info, i->second);

                        index_list.push_back(index_node_linked);
                        hyperlinks_created.insert(*i);
                    } else index_list.back().num_references++;
                    index_node.hyper_links[i->second] = --index_list.end();
                }
            }
        }

        // The node is added to the list
        index_list.push_back(index_node);
        *image_index = --index_list.end();

        return true;
    }

    bool IndexManager::CloseImage(ImageIndex::Ptr &image_index) {
        TRACE("Closing the image '" << image_index->path_name << "'");

        // Decrease the number of references
        image_index->num_references--;

        // If the number of references is zero, then the IndexNode is removed from the list
        if (image_index->num_references == 0) {
            for (size_t i = 0; i < image_index->hyper_links.size(); ++i)
                CloseImage(image_index->hyper_links[i]);
            index_list.erase(image_index);
        }
        return true;
    }

}
