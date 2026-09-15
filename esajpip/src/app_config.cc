#include "app_config.h"

#include <glib.h>

using namespace std;

namespace {

void ReadInteger(GKeyFile *file, const char *group, const char *key, int *value) {
    GError *error = NULL;
    int result = g_key_file_get_integer(file, group, key, &error);
    if (!error)
        *value = result;
    g_clear_error(&error);
}

void ReadString(GKeyFile *file, const char *group, const char *key, string *value) {
    GError *error = NULL;
    char *result = g_key_file_get_string(file, group, key, &error);
    if (!error)
        *value = result;
    g_free(result);
    g_clear_error(&error);
}

}

bool AppConfig::Load(const char *file_name) {
    GKeyFile *file = g_key_file_new();
    bool loaded = g_key_file_load_from_file(file, file_name, G_KEY_FILE_NONE, NULL);
    if (!loaded || !g_key_file_has_group(file, "listen_at") ||
        !g_key_file_has_group(file, "folders") ||
        !g_key_file_has_group(file, "connections") ||
        !g_key_file_has_group(file, "general")) {
        g_key_file_free(file);
        return false;
    }

    ReadInteger(file, "listen_at", "port", &port_);
    ReadString(file, "listen_at", "address", &address_);

    ReadString(file, "folders", "images", &images_folder_);
    ReadString(file, "folders", "logging", &logging_folder_);

    ReadInteger(file, "connections", "identification_time_out", &identification_time_out_);
    ReadInteger(file, "connections", "time_out", &com_time_out_);
    ReadInteger(file, "connections", "max_number", &max_connections_);

    ReadInteger(file, "general", "logging", &logging_);
    ReadInteger(file, "general", "log_requests", &log_requests_);
    ReadInteger(file, "general", "max_chunk_size", &max_chunk_size_);
    g_key_file_free(file);

    if (!images_folder_.empty() && images_folder_.back() != '/')
        images_folder_ += '/';
    if (!logging_folder_.empty() && logging_folder_.back() != '/')
        logging_folder_ += '/';

    return port_ > 0 && port_ <= UINT16_MAX && !images_folder_.empty() &&
           max_chunk_size_ >= 128 && max_connections_ > 0 && identification_time_out_ > 0 &&
           com_time_out_ >= -1 && (logging_ == 0 || logging_ == 1) &&
           (log_requests_ == 0 || log_requests_ == 1);
}
