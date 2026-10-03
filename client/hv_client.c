#include "hv_client.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

struct hv_client {
    hv_cache cache;
    hv_metadata metadata;
    char error[256];
    int pending;
    uint64_t frame;
    int reduce, layers;
};

static int fail(hv_client *client, const char *message) {
    snprintf(client->error, sizeof client->error, "%s", message);
    return -1;
}

hv_client *hv_client_create(void) { return calloc(1, sizeof(hv_client)); }

void hv_client_destroy(hv_client *client) {
    if (!client) return;
    hv_metadata_close(&client->metadata);
    hv_cache_release(&client->cache);
    free(client);
}

const char *hv_client_error(const hv_client *client) { return client->error; }

static int ingest(hv_client *client, const uint8_t *body, size_t size, int restoring) {
    hv_jpp_reader reader;
    hv_jpp_message message;
    int status;
    hv_jpp_begin(&reader, body, size);
    while ((status = hv_jpp_next(&reader, &message)) == HV_JPP_MESSAGE) {
        if (restoring) {
            if (!hv_cache_match_metadata(&client->cache, &message))
                return fail(client, "replacement channel metadata differs from the cache");
        } else if (!hv_cache_apply(&client->cache, &message))
            return fail(client, hv_cache_error(&client->cache));
    }
    if (status == HV_JPP_ERROR) return fail(client, hv_jpp_error(&reader));
    return hv_jpp_reason(&reader);
}

static int completed(int reason) {
    return reason == HV_EOR_WINDOW_DONE || reason == HV_EOR_IMAGE_DONE;
}

int hv_client_response(hv_client *client, const uint8_t *body, size_t size) {
    int reason = ingest(client, body, size, 0);
    if (reason < 0 || !client->pending) return reason;
    if (!completed(reason)) {
        client->pending = HV_CLIENT_READY;
        return reason;
    }
    if (client->pending == HV_CLIENT_FRAME) {
        if (hv_reconstruct_confirm(&client->cache, client->frame, client->reduce, client->layers,
                                   client->error, sizeof client->error) != 0)
            return -1;
    } else {
        hv_status status;
        if (hv_reconstruct_status(&client->cache, client->frame, &status,
                                  client->error, sizeof client->error) != 0) return -1;
        if (!status.resolutions) return fail(client, "the server did not send the frame header");
    }
    client->pending = HV_CLIENT_READY;
    return reason;
}

size_t hv_client_frames(hv_client *client) {
    if (!client->metadata.frames &&
        hv_metadata_open(&client->cache, &client->metadata, client->error, sizeof client->error) != 0)
        return 0;
    return client->metadata.count;
}

int hv_client_status(hv_client *client, uint64_t frame, const hv_client_options *options,
                     hv_client_view *view) {
    hv_client_options defaults = {0};
    if (!options) options = &defaults;
    *view = (hv_client_view){0};
    if (options->reduce < 0 || options->layers < 0 ||
        !isfinite(options->fit_width) || !isfinite(options->fit_height) ||
        options->fit_width < 0 || options->fit_height < 0 ||
        ((options->fit_width > 0) != (options->fit_height > 0)) ||
        (options->fit_width > 0 && options->reduce != 0))
        return fail(client, "invalid frame options");
    if (!hv_client_frames(client)) return -1;
    if (frame >= client->metadata.count) return fail(client, "frame index out of range");
    if (hv_reconstruct_status(&client->cache, frame, &view->source,
                              client->error, sizeof client->error) != 0) return -1;
    view->request = HV_CLIENT_HEADER;
    const hv_status *source = &view->source;
    if (!source->resolutions) return 0;
    int reduce = options->reduce;
    if (reduce >= source->resolutions) reduce = source->resolutions - 1;
    if (options->fit_width > 0) {
        double scale = fmin(options->fit_width / source->width, options->fit_height / source->height);
        while (reduce + 1 < source->resolutions &&
               ceil(ldexp(source->width, -reduce - 1)) >= source->width * scale &&
               ceil(ldexp(source->height, -reduce - 1)) >= source->height * scale)
            reduce++;
    }
    view->reduce = reduce;
    view->width = (uint32_t)ceil(ldexp(source->width, -reduce));
    view->height = (uint32_t)ceil(ldexp(source->height, -reduce));
    view->layers = source->layers;
    for (int r = 0; r < source->resolutions - reduce; r++)
        if (source->quality[r] < view->layers) view->layers = source->quality[r];
    int layers = options->layers;
    if (!layers || layers > source->layers) layers = source->layers;
    view->requested_layers = layers;
    view->ready = view->layers >= layers;
    view->request = view->ready ? HV_CLIENT_READY : HV_CLIENT_FRAME;
    return 0;
}

int hv_client_prepare(hv_client *client, uint64_t frame, const hv_client_options *options,
                      hv_client_view *view) {
    if (client->pending) return fail(client, "a frame request is already pending");
    if (hv_client_status(client, frame, options, view) != 0) return -1;
    client->pending = view->request;
    client->frame = frame;
    client->reduce = view->reduce;
    client->layers = view->requested_layers;
    return 0;
}

int hv_client_model(hv_client *client, size_t *cursor, char *text, size_t capacity) {
    int result = hv_cache_model(&client->cache, cursor, text, capacity);
    if (result < 0) return fail(client, "cache cannot be declared within the request limit");
    return result;
}

int hv_client_restore_response(hv_client *client, const uint8_t *body, size_t size) {
    int reason = ingest(client, body, size, 1);
    if (reason >= 0 && !completed(reason))
        return fail(client, "replacement channel did not complete cache restoration");
    return reason;
}

int hv_client_xml(hv_client *client, uint64_t frame, const uint8_t **xml, size_t *size) {
    if (!hv_client_frames(client)) return -1;
    return hv_metadata_xml(&client->metadata, frame, xml, size, client->error, sizeof client->error);
}

int hv_client_palette(hv_client *client, uint64_t frame, int *channels, uint8_t *table, size_t capacity) {
    if (!hv_client_frames(client)) return -1;
    return hv_metadata_palette(&client->metadata, frame, channels, table, capacity,
                               client->error, sizeof client->error);
}

size_t hv_client_reconstruct(hv_client *client, uint64_t frame, uint8_t *out, size_t capacity) {
    if (!hv_client_frames(client)) return 0;
    if (frame >= client->metadata.count) { fail(client, "frame index out of range"); return 0; }
    return hv_reconstruct(&client->cache, frame, out, capacity, client->error, sizeof client->error);
}
