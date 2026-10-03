/* hv_wasm.c: the WebAssembly module's entry points, for js/jpip_channel.mjs.
 *
 * One module instance is one source: its data-bins and the image last
 * decoded. The host does the HTTP exchange and passes each response body
 * in. Native decoding tests also compile these entry points. In WebAssembly,
 * sizes and addresses are 32 bits, so a host passes them as numbers. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "hv_client.h"
#include "hv_image.h"

#ifdef __wasm__
#define EXPORT(name) __attribute__((export_name(#name))) name
#else
#define EXPORT(name) name
#endif

static hv_client *client;
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
    error[0] = 0;
    if (!client && !(client = hv_client_create())) {
        snprintf(error, sizeof error, "out of memory");
        return -1;
    }
    return hv_client_response(client, body, size);
}

int EXPORT(hv_wasm_restore_response)(const uint8_t *body, size_t size) {
    error[0] = 0;
    return hv_client_restore_response(client, body, size);
}

static char model[1024];
static size_t model_next;

const char *EXPORT(hv_wasm_model)(size_t cursor, size_t capacity) {
    error[0] = 0;
    model_next = cursor;
    if (capacity > sizeof model) capacity = sizeof model;
    if (hv_client_model(client, &model_next, model, capacity) < 0) {
        snprintf(error, sizeof error, "cache cannot be declared within the request limit");
        return NULL;
    }
    return model;
}

size_t EXPORT(hv_wasm_model_next)(void) { return model_next; }

/* Indexes the target's complete metadata once: its codestream count, or 0 with
 * hv_wasm_error. */
uint32_t EXPORT(hv_wasm_codestreams)(void) {
    error[0] = 0;
    size_t count = hv_client_frames(client);
    return count > UINT32_MAX ? UINT32_MAX : (uint32_t)count;
}

/* The XML of a codestream (hv_metadata_xml), good until hv_wasm_reset:
 * its size, 0 when it has none, or -1 with hv_wasm_error;
 * and where it is. */
static const uint8_t *xml;

int EXPORT(hv_wasm_xml_size)(uint32_t codestream) {
    error[0] = 0;
    if (!client) return -1;
    size_t size;
    if (hv_client_xml(client, codestream, &xml, &size) != 0)
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
    error[0] = 0;
    if (!client) return -1;
    return hv_client_palette(client, codestream, &palette_channels, palette, sizeof palette);
}

int EXPORT(hv_wasm_palette_channels)(void) {
    return palette_channels;
}

const uint8_t *EXPORT(hv_wasm_palette_table)(void) {
    return palette;
}

/* Inspect or prepare a frame request. JavaScript reads the fixed-width view;
 * the client owns the pending request and confirms its completed response. */
const hv_client_view *EXPORT(hv_wasm_view)(uint32_t codestream, int reduce,
                                          double width, double height, int layers, int prepare) {
    error[0] = 0;
    static hv_client_view view;
    if (!client) return NULL;
    hv_client_options options = {reduce, width, height, layers};
    int result = prepare ? hv_client_prepare(client, codestream, &options, &view)
                         : hv_client_status(client, codestream, &options, &view);
    return result == 0 ? &view : NULL;
}

/* Decodes a codestream of the store without its `reduce` highest
 * resolutions (hv_reconstruct, hv_image_decode): 0, with the image in the
 * accessors below until the next call, or -1 with hv_wasm_error. */
int EXPORT(hv_wasm_decode)(uint32_t codestream, int reduce) {
    error[0] = 0;
    if (!client) return -1;
    size_t size = hv_client_reconstruct(client, codestream, NULL, 0);
    uint8_t *bytes;
    int status = -1;
    int channels, entries = hv_client_palette(client, codestream, &channels, NULL, 0);

    free(image.pixels);
    image.pixels = NULL;
    if (size == 0 || entries < 0)
        return -1;
    if ((bytes = malloc(size)) == NULL) {
        snprintf(error, sizeof error, "out of memory");
        return -1;
    }
    if (hv_client_reconstruct(client, codestream, bytes, size) == size)
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
    return error[0] ? error : client ? hv_client_error(client) : "client is closed";
}

/* Forgets the channel: its data-bins and its image. */
void EXPORT(hv_wasm_reset)(void) {
    hv_client_destroy(client);
    client = NULL;
    free(image.pixels);
    image.pixels = NULL;
    xml = NULL;
}
