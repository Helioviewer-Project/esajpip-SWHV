/* hv_transcode: rewrites JPEG 2000 files for JHelioviewer and the JPIP
 * server, as hvJP2K's jp2_transcode does: the codestream in RPCL order with
 * the given precincts and PLT markers, without recompressing it.
 *
 *   hv_transcode [-x] [-p W,H] input output
 *
 *   -p W,H  precinct width and height, powers of 2 from 2 to 32768
 *           (default 128,128)
 *   -x      rewrite the first top-level XML box as its root element alone
 *           (see hv_transcode_file)
 *
 * The input must be a JP2 file that the output can serve: within the JPIP
 * server's profile (JPIP_PROFILE.md) except for its tile-parts, which are
 * rewritten. JPX files and raw codestreams are rejected. The boxes are
 * kept: the codestream box is transcoded and every other box is copied as
 * read. The output is written to a temporary file next to it and renamed
 * into place, so input and output may be the same file. Exit status: 0 on
 * success, 1 on error, 2 on usage errors. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hv_writer.h"
#include "transcode.h"

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

    hv_out_init(&out);
    status = hv_transcode_file(buf, size, ppx, ppy, xml_rewrite, &out, error, sizeof error);
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
