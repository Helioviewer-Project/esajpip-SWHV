#ifndef _SERVER_CHANNEL_ENGINE_H_
#define _SERVER_CHANNEL_ENGINE_H_

#include <cstddef>
#include <string>
#include <vector>

#include <zlib.h>

#include "jpeg2000/file_manager.h"
#include "jpip/databin_server.h"
#include "jpip/request.h"

class ChannelEngine {
public:
    enum class GenerateResult {
        MORE,
        COMPLETE,
        FAILED
    };

private:
    jpeg2000::FileManager file_manager;
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
    bool GenerateRawChunk();
    GenerateResult Fail(const char *message);

public:
    explicit ChannelEngine(int _chunk_size);
    ~ChannelEngine();

    bool Init(const std::string &image_directory);
    jpeg2000::FileManager::OpenResult Open(const std::string &target);
    bool Begin(const jpip::Request &request, bool use_gzip,
               std::string *request_error);
    GenerateResult Generate(char *buffer, int capacity, int *length);
    void Finish();

    const std::string &GetError() const {
        return error_message;
    }

    ChannelEngine(const ChannelEngine &) = delete;
    ChannelEngine &operator=(const ChannelEngine &) = delete;
};

#endif /* _SERVER_CHANNEL_ENGINE_H_ */
