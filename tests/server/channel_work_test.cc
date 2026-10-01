#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <uv.h>

#include "channel_fixture.h"
#include "server/channel_work.h"
#include "jpip/request/request.h"

using namespace std;
using channel_test::Check;

struct PooledResponse {
    uv_loop_t loop;
    server::ChannelWork *work = NULL;
    jpip::Request request;
    vector<char> output;
    vector<char> response;
    bool failed = false;

    static void Completed(server::ChannelWork &work, void *owner) {
        PooledResponse *self = static_cast<PooledResponse *>(owner);
        const server::ChannelWork::Result &result = work.GetResult();
        if (!result.error.empty()) {
            cerr << result.error << endl;
            self->failed = true;
            return;
        }

        switch (result.kind) {
        case server::ChannelWork::Kind::OPEN:
            if (result.open !=
                    server::FileManager::OpenResult::OPENED ||
                !work.Begin(std::move(self->request), false,
                            self->output.data(), self->output.size()))
                self->failed = true;
            break;
        case server::ChannelWork::Kind::BEGIN:
        case server::ChannelWork::Kind::GENERATE:
            self->response.insert(self->response.end(), self->output.begin(),
                                  self->output.begin() + result.length);
            if (result.generation ==
                server::ChannelEngine::GenerateResult::MORE) {
                if (!work.Generate(self->output.data(), self->output.size()))
                    self->failed = true;
            } else if (result.generation ==
                       server::ChannelEngine::GenerateResult::COMPLETE) {
                if (!work.Finish())
                    self->failed = true;
            } else {
                self->failed = true;
            }
            break;
        case server::ChannelWork::Kind::FINISH:
            if (!work.Close())
                self->failed = true;
            break;
        case server::ChannelWork::Kind::CLOSE:
            if (!work.IsClosed())
                self->failed = true;
            break;
        }
    }

    explicit PooledResponse(int output_size) : output(output_size) {
    }

    vector<char> Generate(const string &directory) {
        Check(uv_loop_init(&loop) == 0,
              "Could not initialize the channel-work test loop");
        Check(request.ParseTarget(
                      "/jpip?stream=0&metareq=[*]!!&fsiz=1,1&rsiz=1,1&"
                      "roff=0,0&cid=0"),
              "Could not parse the pooled channel request");
        server::ChannelWork channel_work(&loop, 128, directory, Completed,
                                         this);
        work = &channel_work;
        Check(work->IsInitialized() && work->Open("image.jp2"),
              "Could not queue pooled image opening");
        Check(!work->Open("image.jp2"),
              "Queued concurrent work for one channel");
        uv_run(&loop, UV_RUN_DEFAULT);
        Check(!failed && !work->IsActive() && work->IsClosed(),
              "The pooled channel response or terminal cleanup failed");
        work = NULL;
        Check(uv_loop_close(&loop) == 0,
              "The channel-work test loop retained handles");
        return response;
    }
};


struct Completion {
    int calls = 0;
    static void Done(server::ChannelWork &work, void *owner) {
        Check(!work.IsActive(), "Completion called with active work");
        ++static_cast<Completion *>(owner)->calls;
    }
};

// Occupy both workers before queuing the operation to cancel. No sleep is
// used to infer whether the ChannelWork operation has started.
struct Blockers {
    mutex lock;
    condition_variable changed;
    int running = 0;
    int completed = 0;
    bool released = false;
    uv_work_t work[2];
    static void Run(uv_work_t *work) {
        Blockers &self = *static_cast<Blockers *>(work->data);
        unique_lock<mutex> guard(self.lock);
        ++self.running;
        self.changed.notify_all();
        self.changed.wait(guard, [&self] { return self.released; });
    }
    static void Done(uv_work_t *work, int status) {
        Check(status == 0, "A blocking worker failed");
        ++static_cast<Blockers *>(work->data)->completed;
    }
    void Start(uv_loop_t &loop) {
        for (uv_work_t &item : work) {
            item.data = this;
            Check(uv_queue_work(&loop, &item, Run, Done) == 0,
                  "Could not queue blocking worker");
        }
        unique_lock<mutex> guard(lock);
        Check(changed.wait_for(guard, chrono::seconds(5),
                              [this] { return running == 2; }),
              "The two test workers did not start");
    }
    void Release() {
        lock_guard<mutex> guard(lock);
        released = true;
        changed.notify_all();
    }
};

static void CheckQueuedCancellation(const string &directory) {
    uv_loop_t loop;
    Check(uv_loop_init(&loop) == 0, "Could not initialize cancellation loop");
    Completion completion;
    server::ChannelWork work(&loop, 128, directory, Completion::Done, &completion);
    Blockers blockers;
    blockers.Start(loop);
    Check(work.Open("image.jp2"), "Could not queue the canceled open");
    Check(!work.Open("image.jp2") && !work.Finish() && !work.Close(),
          "Active work accepted a second operation");
    work.CancelQueued();
    blockers.Release();
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(completion.calls == 1 && blockers.completed == 2 &&
                  work.GetResult().open == server::FileManager::OpenResult::INVALID,
          "Queued cancellation executed the open or lost its completion");
    Check(work.Finish(), "Could not clean up after queued cancellation");
    work.CancelQueued(); // Cleanup must remain queued, even when canceled.
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(completion.calls == 2 &&
                  work.GetResult().kind == server::ChannelWork::Kind::FINISH,
          "Cleanup cancellation lost its completion");
    Check(work.Open("image.jp2"), "Canceled worker could not be reused");
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(completion.calls == 3 &&
                  work.GetResult().open == server::FileManager::OpenResult::OPENED,
          "Open after cancellation did not complete");
    Check(work.Close(), "Could not close reused worker");
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(completion.calls == 4 && work.IsInitialized() && work.IsClosed() &&
                  work.GetResult().kind == server::ChannelWork::Kind::CLOSE &&
                  !work.Open("image.jp2") && !work.Finish() &&
                  !work.Close() && uv_loop_close(&loop) == 0,
          "Closed worker accepted work or retained loop handles");
}

static void CheckCleanupCancellation(const string &directory) {
    jpip::Request request;
    Check(request.ParseTarget("/jpip?stream=0&metareq=[*]!!&fsiz=1,1&cid=0"),
          "Could not parse cleanup request");
    server::ChannelEngine reference(128);
    string error;
    Check(reference.Init(directory) &&
                  reference.Open("image.jp2") == server::FileManager::OpenResult::OPENED &&
                  reference.Begin(request, false, &error), "Could not prepare cleanup reference");
    char buffer[128]; int length = 0;
    Check(reference.Generate(buffer, sizeof buffer, &length) == server::ChannelEngine::GenerateResult::MORE,
          "Cleanup fixture did not span multiple raw chunks");
    reference.Finish();
    Check(reference.Begin(request, false, &error), "Could not restart cleanup reference");
    vector<char> expected;
    for (;;) {
        server::ChannelEngine::GenerateResult result = reference.Generate(buffer, sizeof buffer, &length);
        Check(result != server::ChannelEngine::GenerateResult::FAILED, "Cleanup reference generation failed");
        expected.insert(expected.end(), buffer, buffer + length);
        if (result == server::ChannelEngine::GenerateResult::COMPLETE) break;
    }

    uv_loop_t loop;
    Check(uv_loop_init(&loop) == 0, "Could not initialize cleanup loop");
    Completion completion;
    server::ChannelWork work(&loop, 128, directory, Completion::Done, &completion);
    Check(work.Open("image.jp2"), "Could not open cleanup source");
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(work.GetResult().open == server::FileManager::OpenResult::OPENED,
          "Cleanup source did not open");
    // This starts compression and advances one raw chunk's cache state, but
    // leaves its gzip stream unfinished. Canceling cleanup would preserve that
    // compression state and corrupt the next response.
    Check(work.Begin(request, true, buffer, 8), "Could not start partial gzip response");
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(work.GetResult().error.empty() && work.GetResult().length == 8 &&
                  work.GetResult().generation == server::ChannelEngine::GenerateResult::MORE,
          "Cleanup fixture did not leave compression active");
    Blockers blockers;
    blockers.Start(loop);
    Check(work.Finish(), "Could not queue compression cleanup");
    work.CancelQueued();
    blockers.Release();
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(work.GetResult().error.empty() && completion.calls == 3,
          "Canceled cleanup did not complete exactly once");
    Check(work.Begin(request, true, buffer, sizeof buffer), "Could not begin gzip after cleanup");
    vector<char> compressed;
    for (;;) {
        uv_run(&loop, UV_RUN_DEFAULT);
        const server::ChannelWork::Result &result = work.GetResult();
        Check(result.error.empty() && result.generation != server::ChannelEngine::GenerateResult::FAILED,
              "Gzip generation after cleanup failed");
        compressed.insert(compressed.end(), buffer, buffer + result.length);
        if (result.generation == server::ChannelEngine::GenerateResult::COMPLETE) break;
        Check(work.Generate(buffer, sizeof buffer), "Could not continue gzip after cleanup");
    }
    Check(channel_test::Gunzip(compressed) == expected,
          "Canceling queued cleanup preserved stale compression or changed the cache");
    Blockers closing_blockers;
    closing_blockers.Start(loop);
    int calls = completion.calls;
    Check(work.Close() && !work.IsClosed(),
          "Could not queue terminal cleanup or closed before its completion");
    work.CancelQueued();
    closing_blockers.Release();
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(completion.calls == calls + 1 && work.IsClosed() &&
                  !work.Begin(request, false, buffer, sizeof buffer) &&
                  !work.Generate(buffer, sizeof buffer) &&
                  uv_loop_close(&loop) == 0,
          "Terminal cleanup was canceled or accepted further generation");
}

static void CheckRunningCancellation(const string &directory) {
    string fifo = directory + "blocked.jp2";
    Check(mkfifo(fifo.c_str(), 0600) == 0, "Could not create worker FIFO");
    uv_loop_t loop;
    Check(uv_loop_init(&loop) == 0, "Could not initialize running-work loop");
    Completion completion;
    server::ChannelWork work(&loop, 128, directory, Completion::Done, &completion);
    Check(work.Open("blocked.jp2"), "Could not queue blocking open");
    int writer = -1;
    chrono::steady_clock::time_point deadline =
            chrono::steady_clock::now() + chrono::seconds(5);
    do {
        writer = open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
        if (writer >= 0) break;
        Check(errno == ENXIO, "Could not synchronize with running open");
        this_thread::sleep_for(chrono::milliseconds(1));
    } while (chrono::steady_clock::now() < deadline);
    Check(writer >= 0, "The blocking open did not start");
    work.CancelQueued();
    Check(work.IsActive() && completion.calls == 0,
          "Running cancellation discarded in-flight work");
    close(writer);
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(completion.calls == 1 &&
                  work.GetResult().open == server::FileManager::OpenResult::INVALID,
          "Running cancellation did not drain the failed FIFO open");
    Check(work.Close(), "Could not close running canceled work");
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(completion.calls == 2 && work.IsClosed() &&
                  uv_loop_close(&loop) == 0,
          "Running canceled work retained loop handles");
}

static void CheckIdleCleanup(const string &directory) {
    uv_loop_t loop;
    Check(uv_loop_init(&loop) == 0, "Could not initialize idle cleanup loop");
    Completion completion;
    server::ChannelWork work(&loop, 128, directory, Completion::Done, &completion);
    Check(work.Open("image.jp2"), "Could not open idle cleanup source");
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(work.GetResult().open == server::FileManager::OpenResult::OPENED &&
                  work.Finish(),
          "Could not queue idle response cleanup");
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(work.IsInitialized() && !work.IsClosed() && work.Close(),
          "Response cleanup lost the engine or blocked terminal cleanup");
    uv_run(&loop, UV_RUN_DEFAULT);
    Check(completion.calls == 3 && work.IsClosed() &&
                  !work.IsActive() && uv_loop_close(&loop) == 0,
          "Idle engine was not closed exactly once");
}

int main() {
    // Must precede the first uv_queue_work in this process.
    Check(setenv("UV_THREADPOOL_SIZE", "2", 1) == 0, "Could not size test worker pool");
    channel_test::Fixture fixture;
    CheckQueuedCancellation(fixture.directory);
    CheckRunningCancellation(fixture.directory);
    CheckCleanupCancellation(fixture.directory);
    CheckIdleCleanup(fixture.directory);
    PooledResponse pooled(128);
    vector<char> response = pooled.Generate(fixture.directory);
    server::ChannelEngine engine(128);
    Check(engine.Init(fixture.directory) &&
                  engine.Open("image.jp2") == server::FileManager::OpenResult::OPENED,
          "Could not open the reference engine");
    jpip::Request request;
    Check(request.ParseTarget("/jpip?stream=0&metareq=[*]!!&fsiz=1,1&rsiz=1,1&roff=0,0&cid=0"),
          "Could not parse reference request");
    string error;
    Check(engine.Begin(request, false, &error), "Could not begin reference response");
    vector<char> expected;
    char buffer[128];
    for (;;) {
        int length = 0;
        server::ChannelEngine::GenerateResult result = engine.Generate(buffer, sizeof buffer, &length);
        Check(result != server::ChannelEngine::GenerateResult::FAILED,
              "Reference response failed");
        expected.insert(expected.end(), buffer, buffer + length);
        if (result == server::ChannelEngine::GenerateResult::COMPLETE) break;
    }
    Check(response == expected, "Pool scheduling changed the generated response");
    return EXIT_SUCCESS;
}
