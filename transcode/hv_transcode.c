/* hv_transcode: rewrites JPEG 2000 files for JHelioviewer and the JPIP
 * server, as hvJP2K's jp2_transcode does: the codestream in RPCL order with
 * the given precincts and PLT markers, without recompressing it.
 *
 *   hv_transcode [-x] [-p W,H] input output
 *
 *   -p W,H  precinct width and height, powers of 2 from 2 to 32768
 *           (default 128,128)
 *   -x      rewrite the first top-level XML box as its root element alone
 *           (see normalize_xml)
 *
 * A JP2 or JPX input keeps its boxes; the first top-level contiguous
 * codestream box is transcoded and every other box is copied as read. A raw
 * codestream (starting with SOC) is transcoded as such. The output is
 * written to a temporary file next to it and renamed into place, so input
 * and output may be the same file. Exit status: 0 on success, 1 on error,
 * 2 on usage errors. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hv_reader.h"
#include "hv_writer.h"
#include "transcode.h"

enum { JP2C = 0x6A703263, XML = 0x786D6C20 };

static void usage(void) {
    fprintf(stderr, "usage: hv_transcode [-x] [-p W,H] input output\n");
}

/* The exponent of a power of 2 from 2 to 32768, or -1. */
static int exponent(const char *s, char **end) {
    long v;
    int e = 0;
    errno = 0;
    v = strtol(s, end, 10);
    if (errno != 0 || *end == s || v < 2 || v > 32768 || (v & (v - 1)) != 0)
        return -1;
    while ((1L << e) < v)
        e++;
    return e;
}

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
 * Files
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

/* Transcodes a JP2/JPX file into out. */
static int transcode_boxes(const uint8_t *buf, size_t size, int ppx, int ppy, int xml_rewrite,
                           hv_out *out, char *error, size_t error_size) {
    hv_boxes it;
    hv_box box;
    const char *message;
    size_t at, start;
    int status, done_jp2c = 0, done_xml = !xml_rewrite;

    hv_boxes_file(&it, buf, size);
    while ((status = hv_boxes_next(&it, &box, &message, &at)) == 1) {
        if (box.type == JP2C && !done_jp2c) {
            /* The transcoded codestream goes straight into its box. */
            if (hv_begin_box(out, JP2C, 0, &start) != 0 ||
                hv_transcode_codestream(buf, box.payload, box.end, ppx, ppy, out, error,
                                        error_size) != 0 ||
                hv_end_box(out, start) != 0)
                return -1;
            done_jp2c = 1;
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
        /* LBox = 0 becomes an explicit length: the box may no longer be
         * the last one. */
        if (box.to_end) {
            if (hv_begin_box(out, box.type, 0, &start) != 0 ||
                hv_write_bytes(out, buf + box.payload, box.end - box.payload) != 0 ||
                hv_end_box(out, start) != 0)
                return -1;
        } else if (hv_write_bytes(out, buf + box.start, box.end - box.start) != 0) {
            return -1;
        }
    }
    if (status < 0) {
        snprintf(error, error_size, "%s at %zu", message, at);
        return -1;
    }
    if (!done_jp2c) {
        snprintf(error, error_size, "no JP2 codestream box");
        return -1;
    }
    return 0;
}

/* Writes out to a temporary file next to `path` with `mode`, then renames
 * it to `path`. */
static int write_file(const char *path, const hv_out *out, mode_t mode, char *error,
                      size_t error_size) {
    size_t n = strlen(path), done = 0;
    char *tmp = malloc(n + 8);
    int fd;

    if (tmp == NULL) {
        snprintf(error, error_size, "out of memory");
        return -1;
    }
    memcpy(tmp, path, n);
    memcpy(tmp + n, ".XXXXXX", 8);
    if ((fd = mkstemp(tmp)) < 0) {
        snprintf(error, error_size, "cannot create %s: %s", tmp, strerror(errno));
        free(tmp);
        return -1;
    }
    if (fchmod(fd, mode) != 0)
        goto failed;
    while (done < out->size) {
        ssize_t w = write(fd, out->data + done, out->size - done);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            goto failed;
        }
        done += (size_t)w;
    }
    if (close(fd) != 0) {
        fd = -1;
        goto failed;
    }
    fd = -1;
    if (rename(tmp, path) != 0)
        goto failed;
    free(tmp);
    return 0;

failed:
    snprintf(error, error_size, "cannot write %s: %s", path, strerror(errno));
    if (fd >= 0)
        close(fd);
    unlink(tmp);
    free(tmp);
    return -1;
}

int main(int argc, char **argv) {
    int ppx = 7, ppy = 7, xml_rewrite = 0, fd, status, i;
    const char *input, *output;
    char error[512];
    struct stat st;
    uint8_t *buf;
    size_t size;
    hv_out out;

    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (strcmp(argv[i], "-x") == 0) {
            xml_rewrite = 1;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            char *end;
            ppx = exponent(argv[++i], &end);
            if (ppx < 0 || *end != ',' || (ppy = exponent(end + 1, &end)) < 0 || *end) {
                fprintf(stderr, "hv_transcode: -p takes W,H: powers of 2 from 2 to 32768\n");
                return 2;
            }
        } else {
            usage();
            return 2;
        }
    }
    if (argc - i != 2) {
        usage();
        return 2;
    }
    input = argv[i];
    output = argv[i + 1];

    if ((fd = open(input, O_RDONLY)) < 0 || fstat(fd, &st) != 0) {
        fprintf(stderr, "hv_transcode: cannot open %s: %s\n", input, strerror(errno));
        return 1;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 2) {
        fprintf(stderr, "hv_transcode: %s: not a JPEG 2000 file\n", input);
        close(fd);
        return 1;
    }
    size = (size_t)st.st_size;
    buf = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (buf == MAP_FAILED) {
        fprintf(stderr, "hv_transcode: cannot map %s: %s\n", input, strerror(errno));
        return 1;
    }

    error[0] = 0;
    hv_out_init(&out);
    if (buf[0] == 0xFF && buf[1] == 0x4F)
        status = hv_transcode_codestream(buf, 0, size, ppx, ppy, &out, error, sizeof error);
    else
        status = transcode_boxes(buf, size, ppx, ppy, xml_rewrite, &out, error, sizeof error);
    if (status != 0 && error[0] == 0)
        snprintf(error, sizeof error, "%s", out.error ? out.error : "out of memory");
    munmap(buf, size);
    if (status == 0)
        status = write_file(output, &out, st.st_mode & 07777, error, sizeof error);
    hv_out_free(&out);
    if (status != 0) {
        fprintf(stderr, "hv_transcode: %s: %s\n", input, error);
        return 1;
    }
    return 0;
}
