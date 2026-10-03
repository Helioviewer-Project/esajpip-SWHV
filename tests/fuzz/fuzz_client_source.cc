#include <algorithm>
#include <climits>

#include "client_support.h"
#include "hv_image.h"

extern "C" int replay_client_source(const uint8_t *data, size_t size) {
    using namespace client_fuzz;
    if (size < 4 || size > 1024 * 1024) return 0;
    Context &fixture = context(data[0]);
    Client client;
    require(hv_client_response(client.value, fixture.opening.data(), fixture.opening.size()) >= 0,
            "opening source failed");
    size_t frames = hv_client_frames(client.value);
    require(frames == fixture.image.GetNumCodestreams(), "fixture frame count differs");
    size_t index = data[1] % frames;
    hv_client_options options = {data[2] == 255 ? INT_MAX : data[2], 0, 0, data[3]};
    hv_client_view view;
    require(hv_client_prepare(client.value, index, &options, &view) == 0 &&
            view.request == HV_CLIENT_FRAME, "fixture frame request failed");
    int reason = hv_client_response(client.value, data + 4, size - 4);
    require(hv_client_status(client.value, index, &options, &view) == 0, "cached source status failed");
    if (reason == HV_EOR_WINDOW_DONE || reason == HV_EOR_IMAGE_DONE)
        require(view.ready, "completed source response did not deliver requested quality");
    hv_client_view before = view;
    size_t cursor = 0;
    for (;;) {
        char model[128], again[128];
        size_t repeated_cursor = cursor;
        int length = hv_client_model(client.value, &cursor, model, sizeof model);
        int repeated_length = hv_client_model(client.value, &repeated_cursor, again, sizeof again);
        require(length == repeated_length && cursor == repeated_cursor &&
                (length < 0 || std::equal(model, model + length + 1, again)),
                "cache model is not stable");
        if (length <= 0) break;
    }
    require(hv_client_restore_response(client.value, fixture.restoration.data(), fixture.restoration.size()) >= 0,
            "unchanged metadata replay rejected");
    require(hv_client_status(client.value, index, &options, &view) == 0 &&
            std::equal(before.source.quality, before.source.quality + 33, view.source.quality) &&
            before.ready == view.ready, "restoration changed cached quality");

    size_t length = hv_client_reconstruct(client.value, index, nullptr, 0);
    if (length && length <= 2 * 1024 * 1024) {
        Bytes bytes(length + 1, 0xA5), again(length);
        require(hv_client_reconstruct(client.value, index, bytes.data(), length) == length &&
                bytes[length] == 0xA5 &&
                hv_client_reconstruct(client.value, index, again.data(), length) == length &&
                std::equal(again.begin(), again.end(), bytes.begin()), "reconstruction is not stable");
        if (static_cast<uint64_t>(view.width) * view.height > 1024 * 1024 ||
            view.source.components > 4) return 0;
        hv_image image;
        char error[256];
        if (hv_image_decode(bytes.data(), length, view.reduce, HV_IMAGE_SAMPLES,
                            &image, error, sizeof error) == 0) {
            require(image.width == view.width && image.height == view.height,
                    "decoded dimensions differ from selected resolution");
            free(image.pixels);
        }
    }
    return 0;
}

#ifndef ESAJPIP_FUZZ_REPLAY
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    return replay_client_source(data, size);
}
#endif
