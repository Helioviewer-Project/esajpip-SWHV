// The native source API, driven by actual server responses without HTTP.
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "hv_client.h"
#include "hv_image.h"
#include "jpeg2000/hv_reader.h"
#include "jpip/index/image_index.h"
#include "jpip/response/databin_server.h"

using Bytes = std::vector<uint8_t>;

static void check(bool ok, const std::string &message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

struct Sources : jpip::SourceProvider {
    struct Entry { std::vector<char> bytes; jpip::Source source; };
    std::map<std::string, Entry> files;
    const jpip::Source *GetSource(const std::string &path) override {
        auto found = files.find(path);
        if (found != files.end()) return &found->second.source;
        std::ifstream input(path, std::ios::binary);
        if (!input) return nullptr;
        Entry &entry = files[path];
        entry.bytes.assign(std::istreambuf_iterator<char>(input), {});
        entry.source = jpip::Source(entry.bytes.data(), entry.bytes.size());
        return &entry.source;
    }
};

static Bytes response(jpip::DataBinServer &server, jpip::ImageIndex &image, Sources &sources,
                      const jpip::ResponseRequest &request) {
    std::string error;
    check(server.SetRequest(image, request, &error), error);
    Bytes body;
    bool last = false;
    while (!last) {
        char chunk[997];
        int size = sizeof chunk;
        check(server.GenerateChunk(sources, chunk, &size, &last), server.GetError());
        const uint8_t *payload = reinterpret_cast<const uint8_t *>(chunk);
        body.insert(body.end(), payload, payload + size);
    }
    return body;
}

static void submit(hv_client *client, const Bytes &body) {
    int reason = hv_client_response(client, body.data(), body.size());
    check(reason == HV_EOR_WINDOW_DONE || reason == HV_EOR_IMAGE_DONE, hv_client_error(client));
}

static jpip::ResponseRequest request(size_t frame, const hv_client_view &view) {
    jpip::ResponseRequest fields;
    fields.AddStream(static_cast<int>(frame), static_cast<int>(frame));
    fields.layers = view.request == HV_CLIENT_HEADER ? 0 : view.requested_layers;
    if (view.request == HV_CLIENT_FRAME) {
        fields.has.fsiz = true;
        fields.resolution_size = jpip::Size(view.width, view.height);
    }
    return fields;
}

static Bytes reconstruct(hv_client *client, size_t frame) {
    size_t size = hv_client_reconstruct(client, frame, nullptr, 0);
    check(size != 0, hv_client_error(client));
    Bytes bytes(size);
    check(hv_client_reconstruct(client, frame, bytes.data(), bytes.size()) == size,
          hv_client_error(client));
    return bytes;
}

static void verify(const char *path, bool jpx) {
    Sources sources;
    jpip::ImageIndex image(path);
    const jpip::Source *file = sources.GetSource(path);
    check(file && image.Open(*file, sources, jpx), image.GetError());
    jpip::DataBinServer server;
    hv_client *client = hv_client_create();
    check(client != nullptr, "client allocation");
    check(hv_client_frames(client) == 0 && hv_client_error(client)[0], "empty metadata accepted");
    hv_client_view view;
    hv_client_options options = {};
    jpip::ResponseRequest opening;
    opening.AddStream(0, 0);
    opening.layers = 0;
    submit(client, response(server, image, sources, opening));
    size_t frames = hv_client_frames(client);
    check(frames == static_cast<size_t>(image.GetNumCodestreams()), "frame count differs");
    const uint8_t *initial_xml;
    size_t initial_xml_size;
    check(hv_client_xml(client, 0, &initial_xml, &initial_xml_size) == 0, hv_client_error(client));
    check(hv_client_status(client, frames, &options, &view) == -1, "invalid frame accepted");
    for (hv_client_options invalid : {hv_client_options{-1, 0, 0, 0},
                                      hv_client_options{0, 1, 0, 0},
                                      hv_client_options{1, 1, 1, 0},
                                      hv_client_options{0, 0, 0, -1}})
        check(hv_client_prepare(client, 0, &invalid, &view) == -1, "invalid options accepted");

    for (size_t frame = 0; frame < frames; frame++) {
        const jpip::CodingParameters &coding = *image.GetCodingParameters(static_cast<int>(frame));
        options = {INT_MAX, 0, 0, 1};
        check(hv_client_prepare(client, frame, &options, &view) == 0, hv_client_error(client));
        if (frame) {
            check(view.request == HV_CLIENT_HEADER && view.width == 0, "missing-header plan differs");
            const uint8_t empty[] = {0, HV_EOR_WINDOW_DONE, 0};
            check(hv_client_response(client, empty, sizeof empty) == -1, "absent header accepted");
            submit(client, response(server, image, sources, request(frame, view)));
            check(hv_client_prepare(client, frame, &options, &view) == 0, hv_client_error(client));
        }
        check(view.request == HV_CLIENT_FRAME && view.reduce == coding.num_levels &&
              view.width == static_cast<uint32_t>(((coding.size.x - 1) >> view.reduce) + 1),
              "lowest-resolution plan differs");
        hv_client_view inspected;
        check(hv_client_status(client, frame, &options, &inspected) == 0, hv_client_error(client));
        check(hv_client_prepare(client, frame, &options, &inspected) == -1, "pending request overwritten");
        const uint8_t limited[] = {0, HV_EOR_BYTE_LIMIT_REACHED, 0};
        check(hv_client_response(client, limited, sizeof limited) == HV_EOR_BYTE_LIMIT_REACHED,
              "limited response reason lost");
        check(hv_client_prepare(client, frame, &options, &view) == 0 && !view.ready,
              "limited response confirmed or prevented retry");
        const uint8_t empty[] = {0, HV_EOR_WINDOW_DONE, 0};
        check(hv_client_response(client, empty, sizeof empty) == -1, "missing precincts confirmed");
        check(hv_client_status(client, frame, &options, &inspected) == 0 && !inspected.ready,
              "rejected response changed readiness");

        // Metadata restoration must neither mutate the source nor finish its
        // pending request; the same request can then be submitted successfully.
        jpip::DataBinServer replacement;
        jpip::ResponseRequest restore;
        restore.AddStream(static_cast<int>(frames), static_cast<int>(frames));
        restore.layers = 0;
        Bytes replay = response(replacement, image, sources, restore);
        check(hv_client_restore_response(client, limited, sizeof limited) == -1,
              "limited restoration accepted");
        check(hv_client_restore_response(client, replay.data(), replay.size()) >= 0,
              hv_client_error(client));
        hv_jpp_reader reader;
        hv_jpp_message message;
        hv_jpp_begin(&reader, replay.data(), replay.size());
        while (hv_jpp_next(&reader, &message) == HV_JPP_MESSAGE)
            if (message.length) {
                replay[static_cast<size_t>(message.data - replay.data())] ^= 1;
                check(hv_client_restore_response(client, replay.data(), replay.size()) == -1,
                      "different metadata accepted");
                break;
            }
        submit(client, response(server, image, sources, request(frame, view)));
        check(hv_client_prepare(client, frame, &options, &view) == 0 && view.ready &&
              view.layers >= 1 && view.request == HV_CLIENT_READY, "preview not confirmed");
        Bytes preview = reconstruct(client, frame);
        check(!preview.empty(), "preview reconstruction failed");

        // Odd dimensions, viewport fit, independent axes, and large layer
        // requests use this frame's own header rather than another frame's.
        options = {0, static_cast<double>(coding.size.x), static_cast<double>(coding.size.y), INT_MAX};
        check(hv_client_prepare(client, frame, &options, &view) == 0 && view.reduce == 0 &&
              view.width == static_cast<uint32_t>(coding.size.x) &&
              view.height == static_cast<uint32_t>(coding.size.y) &&
              view.requested_layers == coding.num_layers, "full-size fit differs");
        if (!view.ready) submit(client, response(server, image, sources, request(frame, view)));
        check(hv_client_status(client, frame, &options, &view) == 0 && view.ready &&
              view.layers == coding.num_layers, "full quality not ready");
        Bytes full = reconstruct(client, frame);
        if (coding.num_levels) {
            int width = ((coding.size.x - 1) >> coding.num_levels) + 1;
            int height = ((coding.size.y - 1) >> coding.num_levels) + 1;
            options = {0, static_cast<double>(width), static_cast<double>(height), 0};
            check(hv_client_status(client, frame, &options, &view) == 0 &&
                  view.reduce == coding.num_levels, "exact lowest-resolution fit differs");
            options.fit_width += 1;
            options.fit_height += 1;
            check(hv_client_status(client, frame, &options, &view) == 0 &&
                  view.reduce < coding.num_levels, "fit above boundary stayed too coarse");
        }
        options = {0, 1, 100000, 1};
        check(hv_client_status(client, frame, &options, &view) == 0 && view.reduce == coding.num_levels &&
              view.ready && view.layers == coding.num_layers, "narrow fit or quality retention differs");

        if (frames == 1) {
            const jpip::Source *original = sources.GetSource(image.GetPathName(static_cast<int>(frame)));
            hv_box box;
            size_t offset = 0;
            check(hv_check_jp2(original->Data(), original->GetSize(), &box, &offset) == nullptr,
                  "original container invalid");
            hv_image decoded, reference;
            char error[256];
            int decoded_result = hv_image_decode(full.data(), full.size(), 0, HV_IMAGE_SAMPLES,
                                                 &decoded, error, sizeof error);
            check(decoded_result == 0, error);
            int reference_result = hv_image_decode(original->Data() + box.payload, box.end - box.payload, 0,
                                                   HV_IMAGE_SAMPLES, &reference, error, sizeof error);
            check(reference_result == 0, error);
            size_t size = static_cast<size_t>(decoded.width) * decoded.height * decoded.components;
            check(decoded.width == reference.width && decoded.height == reference.height &&
                  decoded.components == reference.components &&
                  std::equal(decoded.pixels, decoded.pixels + size, reference.pixels),
                  "native client decoded pixels differ");
            free(decoded.pixels);
            free(reference.pixels);
        }
    }
    size_t cursor = 0;
    char model[128];
    do { check(hv_client_model(client, &cursor, model, sizeof model) >= 0, hv_client_error(client)); }
    while (model[0]);
    const uint8_t *xml;
    size_t size;
    check(hv_client_xml(client, 0, &xml, &size) == 0, hv_client_error(client));
    check(xml == initial_xml && size == initial_xml_size, "borrowed XML changed during refinement");
    int channels;
    check(hv_client_palette(client, 0, &channels, nullptr, 0) >= 0, hv_client_error(client));
    hv_client_destroy(client);
    hv_client_destroy(nullptr);
}

int main() {
    verify(IMAGE, false);
    verify(MOVIE, true);
    return 0;
}
