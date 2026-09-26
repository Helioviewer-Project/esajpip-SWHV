/* hv_served.h: the served profile's checks of a whole file on disk, the
 * files a JPX file links to included, as the server reads it
 * (JPIP_PROFILE.md). For the programs that check files: hv_walk -P and
 * test/test_profile. */
#ifndef HV_SERVED_H
#define HV_SERVED_H

#include <stddef.h>
#include <stdint.h>

#include "hv_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The contents of the file at path, malloc'd (one byte for an empty
 * file), and *size; NULL if it cannot be read. */
uint8_t *hv_load_file(const char *path, size_t *size);

/* Nonzero when path ends in ".jpx", in any case. The server picks the
 * format by the extension: ReadJPX for .jpx, ReadJP2 for .jp2. */
int hv_is_jpx_name(const char *path);

/* What hv_check_served checked, and where it failed. */
typedef struct {
    size_t embedded;            /* codestreams checked in the file itself */
    size_t linked;              /* codestreams checked in linked files */
    size_t at;                  /* the offset of the failure */
    int at_linked;              /* at is in linked_path, not in the file */
    char linked_path[4096];     /* the linked file that failed, or "" */
} hv_served;

/* The served profile's checks of the file at `path`, held in buf:
 * hv_check_jp2, or (jpx) hv_check_jpx; then each codestream with
 * HV_PROFILE, embedded (hv_codestream_check) or linked (hv_link_path from
 * path, hv_load_file, hv_check_link). NULL; otherwise the rule that fails,
 * or the reader's error, with r->at its offset. A link whose path
 * hv_link_path refuses fails with its reason, and one whose file
 * (r->linked_path) cannot be read with url.missing-companion, both at the
 * offset of the link's LOC; a linked file that fails its checks is named
 * in r->linked_path, and r->at_linked is set: r->at is in that file. */
const char *hv_check_served(const char *path, const uint8_t *buf, size_t size, int jpx,
                            hv_served *r);

#ifdef __cplusplus
}
#endif

#endif
