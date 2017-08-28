#include <libconfig.h++>
#include "app_config.h"

using namespace std;
using namespace libconfig;

bool AppConfig::Load(const char *file_name) {
    try {
        size_t n;
        Config cfg;

        cfg.readFile(file_name);
        const Setting &root = cfg.getRoot();

        root["listen_at"].lookupValue("port", port_);
        root["listen_at"].lookupValue("address", address_);

        root["folders"].lookupValue("images", images_folder_);
        root["folders"].lookupValue("caching", caching_folder_);
        root["folders"].lookupValue("logging", logging_folder_);

        if ((n = images_folder_.size()) != 0) {
            if (images_folder_[n - 1] != '/') images_folder_ += '/';
        }

        if ((n = caching_folder_.size()) != 0) {
            if (caching_folder_[n - 1] != '/') caching_folder_ += '/';
        }

        if ((n = logging_folder_.size()) != 0) {
            if (logging_folder_[n - 1] != '/') logging_folder_ += '/';
        }

        root["connections"].lookupValue("time_out", com_time_out_);
        root["connections"].lookupValue("max_number", max_connections_);

        root["general"].lookupValue("logging", logging_);
        root["general"].lookupValue("log_requests", log_requests_);
        root["general"].lookupValue("max_chunk_size", max_chunk_size_);
        root["general"].lookupValue("cache_max_time", cache_max_time_);
    } catch (...) {
        return false;
    }

    return true;
}
