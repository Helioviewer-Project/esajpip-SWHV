/* hv_wasm.c: the WebAssembly module's entry points, for js/jpip_channel.mjs.
 *
 * One module instance is one source: its data-bins and the image last
 * decoded. The host does the HTTP exchange and passes each response body
 * in; this file is compiled for WebAssembly only. Sizes and addresses are
 * 32 bits there, so a host passes them as numbers. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "hv_cache.h"
#include "hv_image.h"
#include "hv_jpp.h"
#include "hv_metadata.h"
#include "hv_reconstruct.h"

#define EXPORT(name) __attribute__((export_name(#name))) name

static hv_cache cache;
static hv_metadata metadata;
static hv_image image;
static char error[256];

/* Memory for the host to place a response body in. */
void *EXPORT(hv_wasm_alloc)(size_t size) {
    return malloc(size);
}

void EXPORT(hv_wasm_free)(void *memory) {
    free(memory);
}

/* Adds the data-bins of one response body to the store: its end-of-response
 * reason (T.808 D.3), or -1 with hv_wasm_error. */
int EXPORT(hv_wasm_response)(const uint8_t *body, size_t size) {
    hv_jpp_reader reader;
    hv_jpp_message message;
    int status;

    hv_jpp_begin(&reader, body, size);
    while ((status = hv_jpp_next(&reader, &message)) == HV_JPP_MESSAGE)
        if (!hv_cache_apply(&cache, &message)) {
            snprintf(error, sizeof error, "%s", hv_cache_error(&cache));
            return -1;
        }
    if (status == HV_JPP_ERROR) {
        snprintf(error, sizeof error, "%s", hv_jpp_error(&reader));
        return -1;
    }
    return hv_jpp_reason(&reader);
}

/* Replacement-channel requests select no codestreams. Any repeated metadata
 * must already be present and identical, not appended a second time. */
int EXPORT(hv_wasm_restore_response)(const uint8_t *body, size_t size) {
    hv_jpp_reader reader;
    hv_jpp_message message;
    int status;
    hv_jpp_begin(&reader, body, size);
    while ((status = hv_jpp_next(&reader, &message)) == HV_JPP_MESSAGE)
        if (!hv_cache_match_metadata(&cache, &message)) {
            snprintf(error, sizeof error, "replacement channel metadata differs from the cache");
            return -1;
        }
    if (status == HV_JPP_ERROR) {
        snprintf(error, sizeof error, "%s", hv_jpp_error(&reader));
        return -1;
    }
    return hv_jpp_reason(&reader);
}

static char model[1024];
static size_t model_next;

const char *EXPORT(hv_wasm_model)(size_t cursor, size_t capacity) {
    model_next = cursor;
    if (capacity > sizeof model) capacity = sizeof model;
    if (hv_cache_model(&cache, &model_next, model, capacity) < 0) {
        snprintf(error, sizeof error, "cache cannot be declared within the request limit");
        return NULL;
    }
    return model;
}

size_t EXPORT(hv_wasm_model_next)(void) { return model_next; }

/* Indexes the target's complete metadata once: its codestream count, or 0 with
 * hv_wasm_error. */
uint32_t EXPORT(hv_wasm_codestreams)(void) {
    if (metadata.frames == NULL && hv_metadata_open(&cache, &metadata, error, sizeof error) != 0)
        return 0;
    return metadata.count > UINT32_MAX ? UINT32_MAX : (uint32_t)metadata.count;
}

/* The XML of a codestream (hv_metadata_xml), good until hv_wasm_reset:
 * its size, 0 when it has none, or -1 with hv_wasm_error;
 * and where it is. */
static const uint8_t *xml;

int EXPORT(hv_wasm_xml_size)(uint32_t codestream) {
    size_t size;
    if (hv_metadata_xml(&metadata, codestream, &xml, &size, error, sizeof error) != 0)
        return -1;
    return size > INT32_MAX ? INT32_MAX : (int)size;
}

const uint8_t *EXPORT(hv_wasm_xml)(void) {
    return xml;
}

/* The color table of a codestream (hv_metadata_palette), good until the
 * next call: its entries, 0 when it has none, or -1 with hv_wasm_error;
 * then the values of an entry, and where the table is. */
static uint8_t palette[HV_PALETTE_MAX];
static int palette_channels;

int EXPORT(hv_wasm_palette)(uint32_t codestream) {
    return hv_metadata_palette(&metadata, codestream, &palette_channels, palette, sizeof palette,
                               error, sizeof error);
}

int EXPORT(hv_wasm_palette_channels)(void) {
    return palette_channels;
}

const uint8_t *EXPORT(hv_wasm_palette_table)(void) {
    return palette;
}

/* A codestream of the store (hv_reconstruct_status): where the 32-bit
 * numbers of its hv_status are, or NULL with hv_wasm_error. */
const hv_status *EXPORT(hv_wasm_status)(uint32_t codestream) {
    static hv_status status;
    int result = hv_reconstruct_status(&cache, codestream, &status, error, sizeof error);
    return result == 0 ? &status : NULL;
}

int EXPORT(hv_wasm_confirm)(uint32_t codestream, int reduce, int layers) {
    return hv_reconstruct_confirm(&cache, codestream, reduce, layers, error, sizeof error);
}

/* Decodes a codestream of the store without its `reduce` highest
 * resolutions (hv_reconstruct, hv_image_decode): 0, with the image in the
 * accessors below until the next call, or -1 with hv_wasm_error. */
int EXPORT(hv_wasm_decode)(uint32_t codestream, int reduce) {
    size_t size = hv_reconstruct(&cache, codestream, NULL, 0, error, sizeof error);
    uint8_t *bytes;
    int status = -1;
    int channels, entries = hv_metadata_palette(&metadata, codestream, &channels, NULL, 0,
                                                error, sizeof error);

    free(image.pixels);
    image.pixels = NULL;
    if (size == 0 || entries < 0)
        return -1;
    if ((bytes = malloc(size)) == NULL) {
        snprintf(error, sizeof error, "out of memory");
        return -1;
    }
    if (hv_reconstruct(&cache, codestream, bytes, size, error, sizeof error) == size)
        status = hv_image_decode(bytes, size, reduce,
                                 entries ? HV_IMAGE_INDICES : HV_IMAGE_SAMPLES,
                                 &image, error, sizeof error);
    free(bytes);
    return status;
}

const uint8_t *EXPORT(hv_wasm_pixels)(void) { return image.pixels; }
uint32_t EXPORT(hv_wasm_width)(void) { return image.width; }
uint32_t EXPORT(hv_wasm_height)(void) { return image.height; }
int EXPORT(hv_wasm_components)(void) { return image.components; }

/* Why the last call failed, NUL-terminated. */
const char *EXPORT(hv_wasm_error)(void) {
    return error;
}

/* Forgets the channel: its data-bins and its image. */
void EXPORT(hv_wasm_reset)(void) {
    hv_metadata_close(&metadata);
    hv_cache_release(&cache);
    free(image.pixels);
    image.pixels = NULL;
    xml = NULL;
}
