#include "server/channel_engine.h"

#include <cstring>

using namespace std;

namespace server {

ChannelEngine::ChannelEngine(int _chunk_size) : chunk_size(_chunk_size) {
    memset(&compression, 0, sizeof compression);
}

ChannelEngine::~ChannelEngine() {
    Finish();
}

bool ChannelEngine::Init(const string &image_directory) {
    return file_manager.Init(image_directory);
}

jpeg2000::FileManager::OpenResult ChannelEngine::Open(const string &target) {
    return file_manager.OpenImage(target);
}

bool ChannelEngine::Begin(const jpip::Request &request, bool use_gzip,
                          string *request_error) {
    error_message.clear();
    gzip = use_gzip;
    raw_last = false;
    return data_server.SetRequest(*file_manager.GetImage(), request,
                                  request_error);
}

ChannelEngine::GenerateResult ChannelEngine::Fail(const char *message) {
    error_message = message;
    return GenerateResult::FAILED;
}

bool ChannelEngine::GenerateSourceChunk(char *buffer, int capacity,
                                        int *length, bool *last) {
    *length = capacity;
    if (!data_server.GenerateChunk(file_manager, buffer, length, last)) {
        error_message = "A new data chunk could not be generated";
        return false;
    }
    if (*length <= 0 && !*last) {
        error_message =
                "No JPIP data chunk was generated before response completion";
        return false;
    }
    return true;
}

ChannelEngine::GenerateResult ChannelEngine::GeneratePlain(
        char *buffer, int capacity, int *length) {
    bool last = false;
    if (!GenerateSourceChunk(buffer, capacity, length, &last))
        return GenerateResult::FAILED;
    return last ? GenerateResult::COMPLETE : GenerateResult::MORE;
}

bool ChannelEngine::GenerateRawChunk() {
    if (raw_buffer.empty())
        raw_buffer.resize(chunk_size);

    int length;
    if (!GenerateSourceChunk(raw_buffer.data(), chunk_size, &length, &raw_last))
        return false;
    compression.next_in = reinterpret_cast<Bytef *>(raw_buffer.data());
    compression.avail_in = length;
    return true;
}

ChannelEngine::GenerateResult ChannelEngine::GenerateGzip(
        char *buffer, int capacity, int *length) {
    if (!compression_active) {
        if (deflateInit2(&compression, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                         MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return Fail("Could not initialize gzip compression");
        compression_active = true;
    }

    compression.next_out = reinterpret_cast<Bytef *>(buffer);
    compression.avail_out = capacity;

    if (compression.avail_in == 0 && !raw_last && !GenerateRawChunk())
        return GenerateResult::FAILED;

    for (;;) {
        int result = deflate(&compression, raw_last ? Z_FINISH : Z_NO_FLUSH);
        if (result == Z_STREAM_END) {
            *length = capacity - compression.avail_out;
            return GenerateResult::COMPLETE;
        }
        if (result != Z_OK)
            return Fail("Could not compress JPIP data");
        if (compression.avail_out == 0) {
            *length = capacity - compression.avail_out;
            return GenerateResult::MORE;
        }
        if (compression.avail_in == 0 && !raw_last && !GenerateRawChunk())
            return GenerateResult::FAILED;
    }
}

ChannelEngine::GenerateResult ChannelEngine::Generate(
        char *buffer, int capacity, int *length) {
    error_message.clear();
    return gzip ? GenerateGzip(buffer, capacity, length)
                : GeneratePlain(buffer, capacity, length);
}

void ChannelEngine::Finish() {
    if (compression_active) {
        deflateEnd(&compression);
        memset(&compression, 0, sizeof compression);
        compression_active = false;
    }
    gzip = false;
    raw_last = false;
    file_manager.ClearFiles();
}

}
