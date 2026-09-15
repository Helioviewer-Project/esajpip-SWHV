#include "app_config.h"

#include <glib.h>

using namespace std;

namespace {

bool ReadInteger(GKeyFile *file, const char *group, const char *key, int *value,
                 string &error_message) {
    if (!g_key_file_has_key(file, group, key, NULL))
        return true;

    GError *error = NULL;
    int result = g_key_file_get_integer(file, group, key, &error);
    if (error) {
        error_message = error->message;
        g_error_free(error);
        return false;
    }
    *value = result;
    return true;
}

bool ReadString(GKeyFile *file, const char *group, const char *key, string *value,
                string &error_message) {
    if (!g_key_file_has_key(file, group, key, NULL))
        return true;

    GError *error = NULL;
    char *result = g_key_file_get_string(file, group, key, &error);
    if (error) {
        error_message = error->message;
        g_error_free(error);
        g_free(result);
        return false;
    }
    *value = result;
    g_free(result);
    return true;
}

}

bool AppConfig::Load(const char *file_name, string &error_message) {
    error_message.clear();
    GKeyFile *file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(file, file_name, G_KEY_FILE_NONE, &error)) {
        error_message = error->message;
        g_error_free(error);
        g_key_file_free(file);
        return false;
    }

    const char *groups[] = {"listen", "jpip", "connections", "logging"};
    for (const char *group : groups) {
        if (!g_key_file_has_group(file, group)) {
            error_message = "missing [" + string(group) + "] section";
            g_key_file_free(file);
            return false;
        }
    }

    bool valid =
        ReadInteger(file, "listen", "port", &port_, error_message) &&
        ReadString(file, "listen", "address", &address_, error_message) &&
        ReadString(file, "jpip", "image_directory", &image_directory_, error_message) &&
        ReadInteger(file, "jpip", "chunk_size", &max_chunk_size_, error_message) &&
        ReadInteger(file, "connections", "initial_timeout", &initial_timeout_, error_message) &&
        ReadInteger(file, "connections", "timeout", &connection_timeout_, error_message) &&
        ReadInteger(file, "connections", "limit", &max_connections_, error_message) &&
        ReadString(file, "logging", "directory", &log_directory_, error_message) &&
        ReadInteger(file, "logging", "file_enabled", &file_logging_, error_message) &&
        ReadInteger(file, "logging", "requests", &log_requests_, error_message);
    g_key_file_free(file);
    if (!valid)
        return false;

    if (port_ <= 0 || port_ > UINT16_MAX)
        error_message = "listen.port must be between 1 and 65535";
    else if (image_directory_.empty())
        error_message = "jpip.image_directory must not be empty";
    else if (max_chunk_size_ < 128)
        error_message = "jpip.chunk_size must be at least 128";
    else if (max_connections_ <= 0)
        error_message = "connections.limit must be positive";
    else if (initial_timeout_ <= 0)
        error_message = "connections.initial_timeout must be positive";
    else if (connection_timeout_ < -1)
        error_message = "connections.timeout must be -1, 0, or positive";
    else if (file_logging_ != 0 && file_logging_ != 1)
        error_message = "logging.file_enabled must be 0 or 1";
    else if (file_logging_ == 1 && log_directory_.empty())
        error_message = "logging.directory must not be empty when file logging is enabled";
    else if (log_requests_ != 0 && log_requests_ != 1)
        error_message = "logging.requests must be 0 or 1";

    if (!error_message.empty())
        return false;

    if (!image_directory_.empty() && image_directory_.back() != '/')
        image_directory_ += '/';
    if (!log_directory_.empty() && log_directory_.back() != '/')
        log_directory_ += '/';

    return true;
}
