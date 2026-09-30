#include <thread>
#include <vector>
#include <zlib.h>

#include "channel_fixture.h"
#include "server/channel_engine.h"
#include "jpip/request/request.h"

using namespace std;
using channel_test::Check;
using channel_test::Gunzip;

static vector<char> GenerateEngineResponse(const string &directory,
                                           bool gzip,
                                           int output_size) {
    server::ChannelEngine engine(128);
    Check(engine.Init(directory), "Could not initialize the channel engine");

    server::FileManager::OpenResult open_result;
    thread open([&] { open_result = engine.Open("image.jp2"); });
    open.join();
    Check(open_result == server::FileManager::OpenResult::OPENED,
          "Could not open an image on a migrated engine thread");

    jpip::Request request;
    Check(request.ParseTarget(
                  "/jpip?stream=0&metareq=[*]!!&fsiz=1,1&rsiz=1,1&"
                  "roff=0,0&cid=0"),
          "Could not parse the migrated engine request");
    bool begun = false;
    string error;
    thread begin([&] { begun = engine.Begin(request, gzip, &error); });
    begin.join();
    Check(begun, "Could not begin a response on a migrated engine thread");

    vector<char> response;
    vector<char> output(output_size);
    server::ChannelEngine::GenerateResult result;
    do {
        int length = 0;
        thread generate([&] {
            result = engine.Generate(output.data(), output.size(), &length);
        });
        generate.join();
        Check(result != server::ChannelEngine::GenerateResult::FAILED,
              "Could not generate data on a migrated engine thread");
        response.insert(response.end(), output.begin(), output.begin() + length);
    } while (result != server::ChannelEngine::GenerateResult::COMPLETE);

    thread finish([&] { engine.Finish(); });
    finish.join();
    return response;
}

int main() {
    channel_test::Fixture fixture;
    vector<char> plain = GenerateEngineResponse(fixture.directory, false, 128);
    Check(!plain.empty(), "The engine generated no response");
    for (int capacity : {1, 8, 128, 1024})
        Check(Gunzip(GenerateEngineResponse(fixture.directory, true, capacity)) == plain,
              "Migrating the channel engine changed the generated response");
    return EXIT_SUCCESS;
}
