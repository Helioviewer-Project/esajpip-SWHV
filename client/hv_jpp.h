/* hv_jpp.h: JPP-stream message reading (T.808 A.2 and D.3).
 *
 * A JPP stream is a sequence of messages. Each message delivers a byte range
 * of one data-bin, and a response ends with one end-of-response message. This
 * parser is the client counterpart of the server's jpip/response/databin_writer.cc
 * and follows the same field layout, but validates rather than generates.
 *
 * The parser holds no allocation beyond one caller-supplied buffer and reports
 * every failure through hv_jpp_error(). A partially parsed response is never
 * left half-committed: hv_jpp_next() either returns a complete message or an
 * error, and the bin store is updated only once the payload has been accepted.
 *
 * Class and CSn are inherited across messages within one response, as the
 * standard specifies; hv_jpp_begin() resets that state.
 */
#ifndef HV_JPP_H
#define HV_JPP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Data-bin classes, from jpip/jpip.h in the server tree. The client keeps its
 * own copy so the module does not link the C++ library. */
enum {
    HV_BIN_PRECINCT = 0,
    HV_BIN_EXTENDED_PRECINCT = 1,
    HV_BIN_TILE_HEADER = 2,
    HV_BIN_TILE_DATA = 4,
    HV_BIN_EXTENDED_TILE = 5,
    HV_BIN_MAIN_HEADER = 6,
    HV_BIN_META_DATA = 8
};

/* End-of-response reasons, from T.808 D.3. */
enum {
    HV_EOR_IMAGE_DONE = 1,
    HV_EOR_WINDOW_DONE = 2,
    HV_EOR_WINDOW_CHANGE = 3,
    HV_EOR_BYTE_LIMIT_REACHED = 4,
    HV_EOR_QUALITY_LIMIT_REACHED = 5,
    HV_EOR_SESSION_LIMIT_REACHED = 6,
    HV_EOR_RESPONSE_LIMIT_REACHED = 7,
    HV_EOR_NON_SPECIFIED = 0xFF
};

/* One delivered data-bin range. The payload is inside the buffer the caller
 * passed to hv_jpp_next(); it stays valid until the next call. */
typedef struct {
    int      bin_class;    /* data-bin class, after inheritance */
    uint64_t codestream;   /* CSn, after inheritance */
    uint64_t bin_id;       /* data-bin identifier */
    uint64_t offset;       /* offset within the data-bin */
    uint64_t length;       /* length of this message's payload */
    int      last_byte;    /* nonzero when this message ends the data-bin */
    const uint8_t *data;   /* payload, length bytes */
} hv_jpp_message;

/* Result of a read. */
enum {
    HV_JPP_MESSAGE = 0,   /* a message was produced */
    HV_JPP_EOR,           /* end of response; check hv_jpp_reason() */
    HV_JPP_ERROR          /* hv_jpp_error() describes the failure */
};

typedef struct {
    /* Input window. hv_jpp_next() never reads past data + size, and reports a
     * truncated message as HV_JPP_ERROR rather than returning partial data. */
    const uint8_t *data;
    size_t         size;
    size_t         position;

    /* Per-response inheritance state, reset by hv_jpp_begin(). */
    uint64_t       cls;
    uint64_t       codestream;
    int            have_previous;

    int      reason;       /* end-of-response reason once HV_JPP_EOR is seen */
    const char *error;     /* NULL unless the last call failed */
} hv_jpp_reader;

/* Prepares a reader over size bytes. Call once per response, or again for a
 * continuation response; class and CSn inheritance restarts each time. */
void hv_jpp_begin(hv_jpp_reader *reader, const uint8_t *data, size_t size);

/* Reads the next message. Returns HV_JPP_MESSAGE, HV_JPP_EOR at the end of the
 * response, or HV_JPP_ERROR. An EOR message must be the last thing in the
 * response: trailing bytes after it are an error. */
int hv_jpp_next(hv_jpp_reader *reader, hv_jpp_message *message);

/* The end-of-response reason after HV_JPP_EOR. Meaningless otherwise. */
int hv_jpp_reason(const hv_jpp_reader *reader);

/* A nonempty diagnostic after HV_JPP_ERROR, empty otherwise. */
const char *hv_jpp_error(const hv_jpp_reader *reader);

/* True when the reason means the response ended normally for this window or
 * byte budget, so the channel may be reused with a further request. Both
 * window done and the limit reasons leave the channel usable; a session limit
 * or a window change does not continue this window. */
int hv_jpp_reason_continues(int reason);

#ifdef __cplusplus
}
#endif
#endif /* HV_JPP_H */