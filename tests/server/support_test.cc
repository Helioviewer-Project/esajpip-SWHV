#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>

#include "server/config.h"
#include "server/storage/file.h"
#include "jpip/response/databin_writer.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static bool LoadConfig(const char *contents, server::Config *config,
                       string *error_message = NULL) {
    char path[] = "/tmp/esajpip-config-XXXXXX";
    int fd = mkstemp(path);
    Check(fd >= 0, "Could not create configuration test file");
    size_t size = strlen(contents);
    Check(write(fd, contents, size) == static_cast<ssize_t>(size),
          "Could not write configuration test file");
    close(fd);

    string error;
    bool loaded = config->Load(path, error);
    remove(path);
    if (error_message)
        *error_message = error;
    return loaded;
}

static void CheckConfig() {
    const char *contents =
        "# Settings may be ordered freely.\n"
        "[logging]\n"
        "file_enabled = true\n"
        "requests = false\n"
        "directory = /var/log/esajpip/\n"
        "cache_max_time = -1\n"
        "\n"
        "[jpip]\n"
        "image_directory = /srv/jpip images\n"
        "chunk_size = 262144\n"
        "\n"
        "[listen]\n"
        "address = 127.0.0.1\n"
        "port = 8090\n"
        "\n"
        "[connections]\n"
        "limit = 250\n"
        "timeout = 60\n"
        "initial_timeout = 4\n"
        "\n"
        "[channels]\n"
        "limit = 500\n";

    server::Config config;
    Check(LoadConfig(contents, &config), "Could not parse INI configuration");
    Check(config.port() == 8090, "Wrong configured port");
    Check(config.address() == "127.0.0.1", "Wrong configured address");
    Check(config.image_directory() == "/srv/jpip images/", "Wrong configured image directory");
    Check(config.log_directory() == "/var/log/esajpip/", "Wrong configured log directory");
    Check(config.max_chunk_size() == 262144,
          "Rejected the maximum configured chunk size");
    Check(config.max_connections() == 250, "Wrong configured connection limit");
    Check(config.max_channels() == 500, "Wrong configured channel limit");
    Check(config.initial_timeout() == 4, "Wrong configured initial timeout");
    Check(config.connection_timeout() == 60, "Wrong configured connection timeout");
    Check(config.file_logging() && !config.log_requests(), "Wrong configured logging flags");

    server::Config missing_group;
    Check(!LoadConfig("[listen]\nport = 8090\n", &missing_group),
          "Accepted configuration with missing groups");

    server::Config invalid;
    Check(!LoadConfig("not an INI file", &invalid), "Accepted malformed configuration");

    // Change exactly one setting in an otherwise valid configuration.
    // An empty diagnostic denotes an accepted boundary value.
    struct Case { const char *setting; const char *replacement; const char *error; };
    const Case cases[] = {
        {"port = 8090", "port = invalid", "port"},
        {"port = 8090", "port = 0", "listen.port must be between 1 and 65535"},
        {"port = 8090", "port = 65536", "listen.port must be between 1 and 65535"},
        {"port = 8090", "port = 1", ""},
        {"port = 8090", "port = 65535", ""},
        {"chunk_size = 262144", "chunk_size = 127", "jpip.chunk_size must be between 128 and 262144"},
        {"chunk_size = 262144", "chunk_size = 262145", "jpip.chunk_size must be between 128 and 262144"},
        {"chunk_size = 262144", "chunk_size = 128", ""},
        {"timeout = 60", "timeout = 0", "connections.timeout must be positive"},
        {"timeout = 60", "timeout = -1", "connections.timeout must be positive"},
        {"initial_timeout = 4", "initial_timeout = 0", "connections.initial_timeout must be positive"},
        {"limit = 250", "limit = 0", "connections.limit must be positive"},
        {"limit = 500", "limit = 0", "channels.limit must be positive"},
        {"image_directory = /srv/jpip images", "image_directory =", "jpip.image_directory must not be empty"},
        {"directory = /var/log/esajpip/", "directory =", "logging.directory must not be empty when file logging is enabled"},
        {"file_enabled = true", "file_enabled = invalid", "file_enabled"}
    };
    for (const Case &test : cases) {
        string changed = contents;
        size_t position = changed.find(test.setting);
        Check(position != string::npos, "Configuration test setting not found");
        changed.replace(position, strlen(test.setting), test.replacement);
        server::Config candidate;
        string error;
        bool accepted = LoadConfig(changed.c_str(), &candidate, &error);
        if (accepted != (test.error[0] == '\0') ||
            (!accepted && error.find(test.error) == string::npos)) {
            cerr << "Configuration case: " << test.replacement << "; got: " << error << endl;
            Check(false, "Unexpected configuration result");
        }
    }
    // Each required key is omitted independently, rather than letting an
    // earlier missing key hide a later one.
    const char *settings[] = {"address = 127.0.0.1", "port = 8090",
        "image_directory = /srv/jpip images", "chunk_size = 262144", "limit = 250",
        "timeout = 60", "initial_timeout = 4", "limit = 500",
        "directory = /var/log/esajpip/", "file_enabled = true", "requests = false"};
    for (const char *setting : settings) {
        string changed = contents;
        size_t position = changed.find(setting);
        Check(position != string::npos, "Required test setting not found");
        changed.erase(position, strlen(setting));
        server::Config candidate;
        string error;
        string key = string(setting).substr(0, string(setting).find(' '));
        if (LoadConfig(changed.c_str(), &candidate, &error) || error.find(key) == string::npos) {
            cerr << "Missing configuration key: " << setting << "; got: " << error << endl;
            Check(false, "Missing setting was not diagnosed");
        }
    }
}

static void CheckMappedSource() {
    server::File file;
    jpip::DataBinWriter writer;
    char buf[32];
    char path[] = "/tmp/esajpip-short-read-XXXXXX";
    int fd = mkstemp(path);
    Check(fd >= 0, "Could not create short payload file");
    Check(write(fd, "x", 1) == 1, "Could not write short payload file");
    close(fd);

    Check(file.Open(path), "Could not open short payload file");
    remove(path);
    char payload = 0;
    Check(file.Read(0, &payload, 1) && payload == 'x',
          "Mapped source lost its bytes after unlinking");
    writer.SetBuffer(buf, sizeof buf);
    Check(writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                       jpip::FileSegment(0, 2), true) ==
              jpip::DataBinWriter::Result::FAILED,
          "Reported a short source-file read as written");
}

int main() {
    CheckConfig();
    CheckMappedSource();
    return EXIT_SUCCESS;
}
