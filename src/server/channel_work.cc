#include "server/channel_work.h"

#include <exception>
#include <utility>

#include "trace.h"

using namespace std;

namespace server {

ChannelWork::ChannelWork(uv_loop_t *_loop, int chunk_size,
                         const string &image_directory, Completed _completed,
                         void *_owner)
    : loop(_loop), engine(chunk_size), completed(_completed), owner(_owner),
      initialized(loop != NULL && completed != NULL &&
                  engine.Init(image_directory)) {
    work.data = this;
}

bool ChannelWork::Open(string image_target) {
    if (active)
        return false;
    target = std::move(image_target);
    return Queue(Kind::OPEN);
}

bool ChannelWork::Begin(jpip::Request image_request, bool use_gzip,
                        char *output, int output_capacity) {
    if (active || output == NULL || output_capacity <= 0)
        return false;
    request = std::move(image_request);
    gzip = use_gzip;
    buffer = output;
    capacity = output_capacity;
    return Queue(Kind::BEGIN);
}

bool ChannelWork::Generate(char *output, int output_capacity) {
    if (active || output == NULL || output_capacity <= 0)
        return false;
    buffer = output;
    capacity = output_capacity;
    return Queue(Kind::GENERATE);
}

bool ChannelWork::Cleanup() {
    return Queue(Kind::CLEANUP);
}

bool ChannelWork::Queue(Kind operation) {
    if (!initialized || active)
        return false;
    result = Result();
    result.kind = operation;
    active = true;
    int status = uv_queue_work(loop, &work, Run, Done);
    if (status == 0)
        return true;
    active = false;
    result.error = uv_strerror(status);
    return false;
}

void ChannelWork::Run(uv_work_t *work) {
    static_cast<ChannelWork *>(work->data)->Perform();
}

void ChannelWork::Perform() {
    try {
        switch (result.kind) {
        case Kind::OPEN:
            result.open = engine.Open(target);
            break;
        case Kind::BEGIN: {
            jpip::Request current = std::move(request);
            if (engine.Begin(current, gzip, &result.error))
                GenerateChunk();
            else
                result.request_rejected = true;
            break;
        }
        case Kind::GENERATE:
            GenerateChunk();
            break;
        case Kind::CLEANUP:
            engine.Finish();
            break;
        }
    } catch (const exception &failure) {
        result.error = failure.what();
    } catch (...) {
        result.error = "Channel processing failed with an unknown exception";
    }
}

void ChannelWork::GenerateChunk() {
    if (!result.error.empty())
        return;
    result.generation = engine.Generate(buffer, capacity, &result.length);
    if (result.generation == ChannelEngine::GenerateResult::FAILED)
        result.error = engine.GetError();
}

void ChannelWork::Done(uv_work_t *work, int status) {
    static_cast<ChannelWork *>(work->data)->Complete(status);
}

void ChannelWork::Complete(int status) {
    active = false;
    if (status != 0 && status != UV_ECANCELED) {
        result.error = uv_strerror(status);
    }
    try {
        completed(*this, owner);
    } catch (const exception &failure) {
        ERROR("Channel completion failed: " << failure.what());
    } catch (...) {
        ERROR("Channel completion failed with an unknown exception");
    }
}

void ChannelWork::CancelQueued() {
    if (active && result.kind != Kind::CLEANUP)
        (void) uv_cancel(reinterpret_cast<uv_req_t *>(&work));
}

bool ChannelWork::IsInitialized() const {
    return initialized;
}

bool ChannelWork::IsActive() const {
    return active;
}

const ChannelWork::Result &ChannelWork::GetResult() const {
    return result;
}

}
