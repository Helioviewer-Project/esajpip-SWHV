#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <zlib.h>

#include "server/storage/file_manager.h"
#include "jpip/response/databin_server.h"
#include "jpip/request/response_request.h"

namespace server {

class ChannelEngine {
public:
    enum class GenerateResult {
        MORE,
        COMPLETE,
        FAILED
    };

private:
    server::FileManager file_manager;
    jpip::DataBinServer data_server;
    std::vector<char> raw_buffer;
    z_stream compression;
    bool gzip = false;
    bool compression_active = false;
    bool raw_last = false;
    std::string error_message;
    int chunk_size;

    GenerateResult GeneratePlain(char *buffer, int capacity, int *length);
    GenerateResult GenerateGzip(char *buffer, int capacity, int *length);
    bool GenerateSourceChunk(char *buffer, int capacity, int *length,
                             bool *last);
    bool GenerateRawChunk();
    GenerateResult Fail(const char *message);
    void Finish();

public:
    explicit ChannelEngine(int _chunk_size);
    ~ChannelEngine();

    bool Init(const std::string &image_directory);
    server::FileManager::OpenResult Open(const std::string &target);
    bool Begin(const jpip::ResponseRequest &request, bool use_gzip,
               std::string *request_error);
    // COMPLETE also releases what the response held: its sources and gzip
    // state.
    GenerateResult Generate(char *buffer, int capacity, int *length);

    const std::string &GetError() const {
        return error_message;
    }

    ChannelEngine(const ChannelEngine &) = delete;
    ChannelEngine &operator=(const ChannelEngine &) = delete;
};

}
