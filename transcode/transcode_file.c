/* transcode_file.c: hv_transcode_file, see transcode.h. */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "hv_reader.h"
#include "transcode.h"

enum { JP2C = 0x6A703263, XML = 0x786D6C20 };

/* ------------------------------------------------------------------------
 * XML (-x)
 * ------------------------------------------------------------------------ */

/* Offset of `s` in p[i, n), or n. */
static size_t find(const uint8_t *p, size_t i, size_t n, const char *s) {
    size_t k = strlen(s);
    for (; i + k <= n; i++)
        if (memcmp(p + i, s, k) == 0)
            return i;
    return n;
}

static int starts(const uint8_t *p, size_t i, size_t n, const char *s) {
    size_t k = strlen(s);
    return i + k <= n && memcmp(p + i, s, k) == 0;
}

/* Just past the '>' that closes the tag at p[i], skipping quoted
 * attribute values, or 0 if unterminated. */
static size_t tag_end(const uint8_t *p, size_t i, size_t n) {
    uint8_t quote = 0;
    for (; i < n; i++) {
        if (quote) {
            if (p[i] == quote)
                quote = 0;
        } else if (p[i] == '"' || p[i] == '\'') {
            quote = p[i];
        } else if (p[i] == '>') {
            return i + 1;
        }
    }
    return 0;
}

/* Just past the markup `open` ... `close` at p[i], or 0 if unterminated. */
static size_t skip(const uint8_t *p, size_t i, size_t n, const char *open, const char *close) {
    size_t j = find(p, i + strlen(open), n, close);
    return j == n ? 0 : j + strlen(close);
}

/* The root element of the XML document p[0, n) in [*start, *end). What
 * hvJP2K writes with --xml-rewrite is glymur's reserialization through
 * lxml, ET.tostring(root, encoding="utf-8"): the root element without the
 * prolog (byte order mark, XML declaration, processing instructions,
 * comments, DOCTYPE) or what follows it. This keeps exactly that and copies
 * the element as read; it does not redo lxml's reserialization of the
 * element itself (attribute quoting, character references, namespace
 * declarations), which leaves already serialized documents unchanged.
 * 0, or -1 when there is no well-formed root element. */
static int xml_root(const uint8_t *p, size_t n, size_t *start, size_t *end) {
    size_t i = starts(p, 0, n, "\xEF\xBB\xBF") ? 3 : 0;
    int depth = 0;

    /* The prolog. */
    for (;;) {
        while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n'))
            i++;
        if (starts(p, i, n, "<?"))
            i = skip(p, i, n, "<?", "?>");
        else if (starts(p, i, n, "<!--"))
            i = skip(p, i, n, "<!--", "-->");
        else if (starts(p, i, n, "<!DOCTYPE")) {
            /* The internal subset may hold '>' inside [ ]. */
            size_t j = i + 9;
            uint8_t quote = 0;
            int bracket = 0;
            for (; j < n; j++) {
                if (quote) {
                    if (p[j] == quote)
                        quote = 0;
                } else if (p[j] == '"' || p[j] == '\'') {
                    quote = p[j];
                } else if (p[j] == '[') {
                    bracket++;
                } else if (p[j] == ']') {
                    bracket--;
                } else if (p[j] == '>' && bracket <= 0) {
                    break;
                }
            }
            i = j < n ? j + 1 : 0;
        } else
            break;
        if (i == 0)
            return -1;
    }
    if (i + 1 >= n || p[i] != '<' || p[i + 1] == '/' || p[i + 1] == '!' || p[i + 1] == '?')
        return -1;
    *start = i;

    /* The root element, by counting start and end tags. */
    while (i < n) {
        if (p[i] != '<') {
            i++;
            continue;
        }
        if (starts(p, i, n, "<!--"))
            i = skip(p, i, n, "<!--", "-->");
        else if (starts(p, i, n, "<![CDATA["))
            i = skip(p, i, n, "<![CDATA[", "]]>");
        else if (starts(p, i, n, "<?"))
            i = skip(p, i, n, "<?", "?>");
        else if (starts(p, i, n, "<!"))
            return -1;
        else {
            size_t j = tag_end(p, i, n);
            if (j == 0)
                return -1;
            if (p[i + 1] == '/')
                depth--;
            else if (p[j - 2] != '/')
                depth++;
            i = j;
            if (depth == 0) {
                *end = i;
                return 0;
            }
            continue;
        }
        if (i == 0)
            return -1;
    }
    return -1;
}

/* ------------------------------------------------------------------------
 * Boxes
 * ------------------------------------------------------------------------ */

/* Checks the boxes inside superboxes, which are copied as read. */
static int check_children(const uint8_t *buf, const hv_box *parent, char *error, size_t size) {
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at;
    int status;

    hv_boxes_children(&it, buf, parent);
    while ((status = hv_boxes_next(&it, &box, &message, &at)) == 1)
        if (hv_is_superbox(box.type) && check_children(buf, &box, error, size) != 0)
            return -1;
    if (status < 0)
        snprintf(error, size, "%s at %zu", message, at);
    return status < 0 ? -1 : 0;
}

/* Transcodes a JP2 file that passed hv_check_jp2 into out. The boxes come
 * out in the order they went in, so a box with LBox = 0, the last one, is
 * still last and is copied as read; so are the children of a superbox. The
 * codestream and XML boxes, rewritten, get explicit lengths. */
static int transcode_boxes(const uint8_t *buf, size_t size, int ppx, int ppy, int xml_rewrite,
                           hv_out *out, char *error, size_t error_size) {
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at, start;
    int status, done_xml = !xml_rewrite;

    hv_boxes_file(&it, buf, size);
    while ((status = hv_boxes_next(&it, &box, &message, &at)) == 1) {
        if (box.type == JP2C) {
            /* The transcoded codestream goes straight into its box. */
            if (hv_begin_box(out, JP2C, 0, &start) != 0 ||
                hv_transcode_codestream(buf, box.payload, box.end, ppx, ppy, HV_PROFILE_HEADERS,
                                        out, error, error_size) != 0 ||
                hv_end_box(out, start) != 0)
                return -1;
            continue;
        }
        if (box.type == XML && !done_xml) {
            size_t x0, x1;
            if (xml_root(buf + box.payload, box.end - box.payload, &x0, &x1) != 0) {
                snprintf(error, error_size, "XML box at %zu has no well-formed root element",
                         box.start);
                return -1;
            }
            if (hv_begin_box(out, XML, 0, &start) != 0 ||
                hv_write_bytes(out, buf + box.payload + x0, x1 - x0) != 0 ||
                hv_end_box(out, start) != 0)
                return -1;
            done_xml = 1;
            continue;
        }
        if (hv_is_superbox(box.type) && check_children(buf, &box, error, error_size) != 0)
            return -1;
        if (hv_write_bytes(out, buf + box.start, box.end - box.start) != 0)
            return -1;
    }
    if (status < 0) {       /* not reached: hv_check_jp2 read the same boxes */
        snprintf(error, error_size, "%s at %zu", message, at);
        return -1;
    }
    return 0;
}

int hv_transcode_file(const uint8_t *buf, size_t size, int ppx, int ppy, int xml_rewrite,
                      hv_out *out, char *error, size_t error_size) {
    size_t out_start = out->size, at;
    const char *rule;
    hv_box jp2c;
    int status;

    error[0] = 0;
    if ((rule = hv_check_jp2(buf, size, &jp2c, &at)) != NULL ||
        (rule = hv_check_jp2h(buf, size, &at)) != NULL) {
        snprintf(error, error_size, "%s at %zu", rule, at);
        return -1;
    }
    status = transcode_boxes(buf, size, ppx, ppy, xml_rewrite, out, error, error_size);
    if (status == 0 && out->size - out_start > INT_MAX) {
        snprintf(error, error_size, "file.size-limit: output larger than INT_MAX bytes");
        status = -1;
    }
    if (status != 0) {
        if (error[0] == 0)
            snprintf(error, error_size, "%s", out->error ? out->error : "out of memory");
        out->size = out_start;
    }
    return status;
}
