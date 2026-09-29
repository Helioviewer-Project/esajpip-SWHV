#include "file_manager.h"

#include <climits>
#include <utility>

using namespace std;

namespace server {

    static bool HasParentSegment(const string &path) {
        size_t begin = 0;
        while (begin <= path.size()) {
            size_t end = path.find('/', begin);
            if (end == string::npos)
                end = path.size();
            if (end - begin == 2 && path[begin] == '.' && path[begin + 1] == '.')
                return true;
            if (end == path.size())
                break;
            begin = end + 1;
        }
        return false;
    }

    FileManager::OpenResult FileManager::OpenImage(const string &path_image_file) {
        if (path_image_file.empty()) {
            ERROR("The image file name is empty");
            return OpenResult::INVALID_PATH;
        }
        if (path_image_file.find('\0') != string::npos ||
            HasParentSegment(path_image_file)) {
            ERROR("Invalid image file path: '" << path_image_file << "'");
            return OpenResult::INVALID_PATH;
        }
        string path = path_image_file;
        if (path[0] == '/')
            path.erase(0, 1);
        path.insert(0, root_dir_);

        unique_ptr<jpip::ImageIndex> image_index(new jpip::ImageIndex(path));
        OpenResult result = ReadImage(path, image_index.get());
        ClearFiles();
        if (result != OpenResult::OPENED)
            return result;
        image = std::move(image_index);
        return OpenResult::OPENED;
    }

    FileManager::OpenResult FileManager::ReadImage(const string &path, jpip::ImageIndex *index) {
        size_t dot = path.find_last_of('.');
        string extension = dot == string::npos ? "" : path.substr(dot);
        if (extension != ".jp2" && extension != ".jpx") return OpenResult::UNSUPPORTED;
        File file;
        File::OpenResult opened = file.Open(path.c_str(), INT_MAX);
        if (opened == File::OpenResult::NOT_FOUND) return OpenResult::NOT_FOUND;
        if (opened == File::OpenResult::EMPTY || opened == File::OpenResult::TOO_LARGE)
            return OpenResult::INVALID;
        if (opened != File::OpenResult::OPENED) return OpenResult::UNREADABLE;
        if (!index->Open(file, *this, extension == ".jpx")) {
            ERROR("Cannot index '" << path << "': " << index->GetError());
            return OpenResult::INVALID;
        }
        return OpenResult::OPENED;
    }
}
