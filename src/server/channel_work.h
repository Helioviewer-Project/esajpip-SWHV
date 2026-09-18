#ifndef _SERVER_CHANNEL_WORK_H_
#define _SERVER_CHANNEL_WORK_H_

#include <cstddef>
#include <string>

#include <uv.h>

#include "jpip/request.h"
#include "server/channel_engine.h"

namespace server {

class ChannelWork {
public:
    enum class Kind {
        NONE,
        OPEN,
        BEGIN,
        GENERATE,
        CLEANUP
    };

    struct Result {
        Kind kind = Kind::NONE;
        jpeg2000::FileManager::OpenResult open =
                jpeg2000::FileManager::OpenResult::INVALID;
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
    ChannelEngine engine;
    Completed completed;
    void *owner;
    jpip::Request request;
    std::string target;
    Result result;
    char *buffer = NULL;
    int capacity = 0;
    bool gzip = false;
    bool initialized = false;
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
    bool Begin(jpip::Request image_request, bool use_gzip,
               char *output, int output_capacity);
    bool Generate(char *output, int output_capacity);
    bool Cleanup();
    void CancelQueued();

    bool IsInitialized() const;
    bool IsActive() const;
    const Result &GetResult() const;

    ChannelWork(const ChannelWork &) = delete;
    ChannelWork &operator=(const ChannelWork &) = delete;
};

}

#endif /* _SERVER_CHANNEL_WORK_H_ */
