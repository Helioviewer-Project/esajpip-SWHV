#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <uv.h>

#include "jpip/request/response_request.h"
#include "channel_engine.h"

namespace server {

class ChannelWork {
public:
    enum class Kind {
        OPEN,
        BEGIN,
        GENERATE,
        FINISH,
        CLOSE
    };

    struct Result {
        Kind kind = Kind::OPEN;
        server::FileManager::OpenResult open =
                server::FileManager::OpenResult::INVALID;
        ChannelEngine::GenerateResult generation =
                ChannelEngine::GenerateResult::FAILED;
        std::string error;
        int length = 0;
        bool request_rejected = false;
    };

    typedef void (*Completed)(ChannelWork &work, void *owner);

private:
    uv_loop_t *loop;
    uv_work_t work;
    std::unique_ptr<ChannelEngine> engine;
    Completed completed;
    void *owner;
    jpip::ResponseRequest request;
    std::string target;
    Result result;
    char *buffer = NULL;
    int capacity = 0;
    bool gzip = false;
    const bool initialized;
    bool active = false;

    static void Run(uv_work_t *work);
    static void Done(uv_work_t *work, int status);

    bool Queue(Kind operation);
    void Perform();
    void Complete(int status);
    void GenerateChunk();

public:
    ChannelWork(uv_loop_t *_loop, int chunk_size,
                const std::string &image_directory, Completed _completed,
                void *_owner);

    bool Open(std::string image_target);
    bool Begin(jpip::ResponseRequest image_request, bool use_gzip,
               char *output, int output_capacity);
    bool Generate(char *output, int output_capacity);
    // Release response resources while preserving the image and cache.
    bool Finish();
    // Destroy the engine on a worker and prevent further operations.
    bool Close();
    void CancelQueued();

    bool IsInitialized() const;
    bool IsActive() const;
    bool IsClosed() const;
    const Result &GetResult() const;

    ChannelWork(const ChannelWork &) = delete;
    ChannelWork &operator=(const ChannelWork &) = delete;
};

}
