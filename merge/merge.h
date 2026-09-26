/* merge.h: merges JP2 files into a JPX file for the JPIP server, as
 * hvJP2K's hv_jpx_merge does: a JP2 Header box from the first input, then
 * per input a Codestream Header and a Compositing Layer Header box holding
 * what differs from it, the codestream, embedded (jp2c) or linked (ftbl
 * and a url in a final dtbl), and the first XML box, associated with the
 * codestream (asoc, nlst). A port of hvJP2K's jpx_merge.
 *
 * Every input must be a JP2 file within the served profile (hv_check_jp2
 * and HV_PROFILE) with the header boxes T.800 requires (hv_check_jp2h), so
 * the output is a JPX file within the served profile (hv_check_jpx) with
 * valid header boxes (hv_check_jpx_headers), at most INT_MAX bytes. */
#ifndef HV_MERGE_H
#define HV_MERGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    const char *path;       /* for messages, and for links: resolved with realpath */
    const uint8_t *buf;     /* the whole file */
    size_t size;
} hv_merge_input;

/* The inputs, opened when needed: open fills *in for input i, whose path
 * stays valid until hv_merge_files returns and whose bytes stay valid until
 * close; 0, or -1 with a message in error. hv_merge_files opens the first
 * input once, checks it and keeps it open. It opens every later input
 * twice and checks it both times: in the first pass, and in the second,
 * where its size and parsed structure must be as in the first before the
 * output is written from it. At most two inputs are open at a time. */
typedef struct {
    int (*open)(void *context, size_t i, hv_merge_input *in, char *error, size_t error_size);
    void (*close)(void *context, size_t i, hv_merge_input *in);
    void *context;
} hv_merge_inputs;

/* Writes the JPX file merging n inputs to out: their codestreams embedded,
 * or with `links`, linked. 0, or -1 with a message in error; out may then
 * hold part of the file. */
int hv_merge_files(const hv_merge_inputs *inputs, size_t n, int links, FILE *out, char *error,
                   size_t error_size);

/* The same, for inputs already in memory. */
int hv_merge_buffers(const hv_merge_input *inputs, size_t n, int links, FILE *out, char *error,
                     size_t error_size);

#endif
