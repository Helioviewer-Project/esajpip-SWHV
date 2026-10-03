#include <cstring>

#include "client_support.h"
#include "hv_frame.h"

extern "C" int replay_client_response(const uint8_t *data, size_t size) {
    using namespace client_fuzz;
    if (size > 1024 * 1024) return 0;
    Client client;
    hv_cache cache;
    hv_cache_begin(&cache);
    hv_jpp_reader reader;
    hv_jpp_message message;
    hv_jpp_begin(&reader, data, size);
    int status;
    bool accepted = true;
    while ((status = hv_jpp_next(&reader, &message)) == HV_JPP_MESSAGE)
        if (!hv_cache_apply(&cache, &message)) { accepted = false; break; }
    accepted = accepted && status == HV_JPP_EOR;
    int reason = hv_client_response(client.value, data, size);
    require((reason >= 0) == accepted, "source ingestion differs from message ingestion");
    if (accepted) require(reason == hv_jpp_reason(&reader), "source changed EOR reason");

    hv_metadata metadata = {};
    char error[256];
    bool indexed = hv_metadata_open(&cache, &metadata, error, sizeof error) == 0;
    size_t frames = hv_client_frames(client.value);
    require(frames == (indexed ? metadata.count : 0), "source metadata indexing differs");
    for (size_t i = 0; indexed && i < metadata.count && i < 16; i++) {
        const uint8_t *xml, *reference;
        size_t length, reference_length;
        require(hv_client_xml(client.value, i, &xml, &length) == 0 &&
                hv_metadata_xml(&metadata, i, &reference, &reference_length, error, sizeof error) == 0 &&
                length == reference_length && (!length || std::memcmp(xml, reference, length) == 0),
                "source XML differs");
        int channels, reference_channels;
        uint8_t table[HV_PALETTE_MAX], reference_table[HV_PALETTE_MAX];
        int entries = hv_client_palette(client.value, i, &channels, table, sizeof table);
        int reference_entries = hv_metadata_palette(&metadata, i, &reference_channels,
                                                    reference_table, sizeof reference_table,
                                                    error, sizeof error);
        require(entries == reference_entries, "source palette result differs");
        if (entries > 0)
            require(channels == reference_channels &&
                    std::memcmp(table, reference_table, static_cast<size_t>(entries) * channels) == 0,
                    "source palette differs");

        // Bound expensive traversal by decoded geometry, not by input size.
        // Invalid headers still reach the geometry parser; large valid geometry
        // is skipped here rather than consuming the campaign in long loops.
        const hv_bin *header = hv_cache_find(&cache, HV_BIN_MAIN_HEADER, i, 0);
        if (!header || !header->complete) continue;
        const hv_frame *frame = hv_frame_get(&cache, i, error, sizeof error);
        if (!frame || frame->precinct_end[frame->resolutions - 1] > 4096 ||
            static_cast<uint64_t>(frame->width) * frame->height > 1024 * 1024 ||
            frame->components > 4 || frame->layers > 32) continue;
        hv_client_view view;
        require(hv_client_status(client.value, i, nullptr, &view) == 0, "source status failed");
        hv_status reference_status;
        require(hv_reconstruct_status(&cache, i, &reference_status, error, sizeof error) == 0 &&
                std::memcmp(&view.source, &reference_status, sizeof reference_status) == 0,
                "source status differs");
        size_t length_j2k = hv_client_reconstruct(client.value, i, nullptr, 0);
        require(length_j2k == hv_reconstruct(&cache, i, nullptr, 0, error, sizeof error),
                "source reconstruction size differs");
        if (length_j2k && length_j2k <= 2 * 1024 * 1024) {
            Bytes out(length_j2k), expected(length_j2k);
            require(hv_client_reconstruct(client.value, i, out.data(), out.size()) == out.size() &&
                    hv_reconstruct(&cache, i, expected.data(), expected.size(), error, sizeof error) == expected.size() &&
                    out == expected, "source reconstructed bytes differ");
        }
    }
    hv_metadata_close(&metadata);
    hv_cache_release(&cache);
    return 0;
}

#ifndef ESAJPIP_FUZZ_REPLAY
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    return replay_client_response(data, size);
}
#endif
