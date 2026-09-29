/* hv_served.h: the served profile's checks of a whole file on disk, the
 * files a JPX file links to included, as the server reads it
 * (JPIP_PROFILE.md). For the programs that check files: hv_walk -P and
 * test/test_profile. They read each file whole into memory; a server that
 * maps its files uses hv_check_jp2, hv_check_jpx, hv_link_path,
 * hv_path_within and hv_check_link on the mapping instead. */
#ifndef HV_SERVED_H
#define HV_SERVED_H

#include <stddef.h>
#include <stdint.h>

#include "hv_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The contents of the regular file at path, malloc'd (one byte for an
 * empty file), and *size; NULL, with errno set, if it cannot be read, is
 * no regular file (EINVAL) or is larger than max bytes (EFBIG, before
 * anything is allocated). It is opened without blocking, so a FIFO or a
 * device fails at once instead of hanging or never ending. The readers
 * take at most INT_MAX bytes (file.size-limit). */
uint8_t *hv_load_file(const char *path, size_t max, size_t *size);

/* Where the file at path lies against the directory root, both with their
 * symbolic links, "." and ".." resolved (realpath): 1 inside it, at any
 * depth; 0 outside; -1 if either cannot be resolved (errno set: ENOENT
 * for a missing file). *resolved (unless NULL) gets path resolved,
 * malloc'd, when inside. A link's path can name any file, "../" and
 * absolute paths included, so a server confines links to its document
 * root with this. */
int hv_path_within(const char *path, const char *root, char **resolved);

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
 * path, hv_load_file, hv_check_link). With `root` (else NULL), a linked
 * file must lie inside that directory (hv_path_within), and is read
 * through its resolved path; a root that cannot be resolved fails with
 * "root directory cannot be resolved". NULL, with r->at 0 and
 * r->linked_path ""; otherwise the rule that fails, or the reader's
 * error, with r->at its offset. A link whose path hv_link_path refuses
 * fails with its reason, one outside root with "linked file outside the
 * root directory", and one whose file (r->linked_path) cannot be read, or
 * is no regular file, with url.missing-companion, all at the offset of
 * the link's LOC; one larger than INT_MAX bytes fails with
 * file.size-limit at offset 0 of that file, before it is read; a linked
 * file that fails its checks is named in r->linked_path, and r->at_linked
 * is set: r->at is in that file. r->embedded and r->linked count the
 * codestreams that passed. A linked file is read and checked once, however
 * many links name it: the same file (device and inode) of the same size,
 * with the same fragment, passes again unread. Otherwise a JPX file of a
 * few bytes per link could make it read one large file over and over; a
 * server that checks the links on its mappings needs the same care. */
const char *hv_check_served(const char *path, const uint8_t *buf, size_t size, int jpx,
                            const char *root, hv_served *r);

#ifdef __cplusplus
}
#endif

#endif
