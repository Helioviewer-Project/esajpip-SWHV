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
    if (!loaded || !g_key_file_has_group(file, "listen") ||
        !g_key_file_has_group(file, "jpip") ||
        !g_key_file_has_group(file, "connections") ||
        !g_key_file_has_group(file, "logging")) {
        g_key_file_free(file);
        return false;
    }

    ReadInteger(file, "listen", "port", &port_);
    ReadString(file, "listen", "address", &address_);

    ReadString(file, "jpip", "image_directory", &image_directory_);
    ReadInteger(file, "jpip", "chunk_size", &max_chunk_size_);

    ReadInteger(file, "connections", "initial_timeout", &initial_timeout_);
    ReadInteger(file, "connections", "timeout", &connection_timeout_);
    ReadInteger(file, "connections", "limit", &max_connections_);

    ReadString(file, "logging", "directory", &log_directory_);
    ReadInteger(file, "logging", "file_enabled", &file_logging_);
    ReadInteger(file, "logging", "requests", &log_requests_);
    g_key_file_free(file);

    if (!image_directory_.empty() && image_directory_.back() != '/')
        image_directory_ += '/';
    if (!log_directory_.empty() && log_directory_.back() != '/')
        log_directory_ += '/';

    return port_ > 0 && port_ <= UINT16_MAX && !image_directory_.empty() &&
           max_chunk_size_ >= 128 && max_connections_ > 0 && initial_timeout_ > 0 &&
           connection_timeout_ >= -1 && (file_logging_ == 0 || file_logging_ == 1) &&
           (log_requests_ == 0 || log_requests_ == 1);
}
