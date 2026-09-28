/* transcode_file.c: hv_transcode_file, see transcode.h. */
#include <limits.h>
#include <string.h>

#include "hv_error.h"
#include "hv_reader.h"
#include "transcode.h"

/* ------------------------------------------------------------------------
 * Boxes
 * ------------------------------------------------------------------------ */

/* Checks the boxes inside superboxes, which are copied as read; parent is
 * at nesting depth `depth`. */
static int check_children(const uint8_t *buf, const hv_box *parent, int depth, char *error,
                          size_t size) {
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at;
    int status;

    hv_boxes_children(&it, buf, parent);
    while ((status = hv_boxes_next(&it, &box, &message, &at)) == 1) {
        if (!hv_is_superbox(box.type))
            continue;
        if (depth + 1 > HV_BOX_DEPTH_MAX)
            return hv_fail(error, size, "boxes nested deeper than %d at %zu", HV_BOX_DEPTH_MAX,
                           box.start);
        if (check_children(buf, &box, depth + 1, error, size) != 0)
            return -1;
    }
    return status < 0 ? hv_fail(error, size, "%s at %zu", message, at) : 0;
}

/* Transcodes a JP2 file that passed hv_check_jp2 into out. The boxes come
 * out in the order they went in, so a box with LBox = 0, the last one, is
 * still last and is copied as read; so are the children of a superbox. The
 * codestream, and a top-level XML box cut at a NUL, get explicit lengths. */
static int transcode_boxes(const uint8_t *buf, size_t size, int ppx, int ppy, hv_out *out,
                           char *error, size_t error_size) {
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at, start;
    int status;

    hv_boxes_file(&it, buf, size);
    while ((status = hv_boxes_next(&it, &box, &message, &at)) == 1) {
        if (box.type == HV_BOX_JP2C) {
            /* The transcoded codestream goes straight into its box. */
            if (hv_begin_box(out, HV_BOX_JP2C, 0, &start) != 0 ||
                hv_transcode_codestream(buf, box.payload, box.end, ppx, ppy, HV_PROFILE_HEADERS,
                                        out, error, error_size) != 0 ||
                hv_end_box(out, start) != 0)
                return -1;
            continue;
        }
        if (box.type == HV_BOX_XML) {
            /* An XML box holds a well-formed XML document (T.800 I.7.1),
             * which has no NUL (XML 1.0, Char); old Kakadu, in IDL, ended
             * its XML boxes with one. The box ends before its first NUL;
             * one without is copied as read, below. */
            const uint8_t *nul = memchr(buf + box.payload, 0, box.end - box.payload);
            if (nul != NULL) {
                size_t n = (size_t)(nul - (buf + box.payload));
                if (hv_write_box_header(out, HV_BOX_XML, n) != 0 ||
                    hv_write_bytes(out, buf + box.payload, n) != 0)
                    return -1;
                continue;
            }
        }
        if (hv_is_superbox(box.type) && check_children(buf, &box, 1, error, error_size) != 0)
            return -1;
        if (hv_write_bytes(out, buf + box.start, box.end - box.start) != 0)
            return -1;
    }
    /* status < 0 is not reached: hv_check_jp2 read the same boxes. */
    return status < 0 ? hv_fail(error, error_size, "%s at %zu", message, at) : 0;
}

int hv_transcode_file(const uint8_t *buf, size_t size, int ppx, int ppy, hv_out *out,
                      char *error, size_t error_size) {
    size_t out_start = out->size, at;
    const char *rule;
    hv_box jp2c;
    int status;

    error[0] = 0;
    if (out->error != NULL)                     /* an earlier write failed */
        return hv_fail(error, error_size, "%s", out->error);
    /* The codestream as the transcode reads it (transcode.c: the flags of
     * its hv_codestream_open, trailing zero PLT entries accepted), before
     * the header boxes, which hv_check_jp2h checks against it. */
    if ((rule = hv_check_jp2(buf, size, &jp2c, &at)) != NULL ||
        (rule = hv_codestream_check(buf, jp2c.payload, jp2c.end,
                                    HV_PROFILE_HEADERS | HV_ACCEPT_PLT_PADDING, &at)) != NULL ||
        (rule = hv_check_jp2h(buf, size, &at)) != NULL)
        return hv_fail(error, error_size, "%s at %zu", rule, at);
    status = transcode_boxes(buf, size, ppx, ppy, out, error, error_size);
    if (status == 0 && out->size - out_start > INT_MAX)
        status = hv_fail(error, error_size, "file.size-limit: output larger than INT_MAX bytes");
    if (status != 0) {
        if (error[0] == 0)
            hv_fail(error, error_size, "%s", out->error ? out->error : "out of memory");
        hv_out_rewind(out, out_start);
    }
    return status;
}
