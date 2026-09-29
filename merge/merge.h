/* merge.h: merges JP2 files into a JPX file for the JPIP server, as
 * hvJP2K's hv_jpx_merge does: a JP2 Header box from the first input, then
 * per input a Codestream Header and a Compositing Layer Header box holding
 * what differs from it, the codestream, embedded (jp2c) or linked (ftbl
 * and a url in a final dtbl), and the first XML box, associated with the
 * codestream (asoc, nlst). A port of hvJP2K's jpx_merge.
 *
 * Every input must pass the served profile's container checks (hv_check_jp2)
 * and the header checks (hv_read_jp2h). The default leaves the remaining
 * codestream opaque. With validate nonzero, HV_PROFILE checks the complete
 * codestream too, including PLT. Output container and header checks are the
 * same in both modes (hv_check_jpx, hv_check_jpx_headers). What
 * a JP2 file may hold and the JPX file may not is rejected too: in the
 * first input more than one colr box with the same METH (colr.one-method,
 * as the JPX file's jp2h), and in a later input no cdef or res box where
 * the first has one (its jplh would inherit the first's). Limits: at most
 * 16 colr boxes per input, 16,777,215 inputs, 65,535 when linked. */
#ifndef HV_MERGE_H
#define HV_MERGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* One immutable input: its bytes and size must remain unchanged throughout
 * hv_merge_files or hv_merge_buffers, including between acquisitions.
 * Mutation is a configuration/operational error outside this contract.
 * With links, buf must hold the file at path, and that file must remain
 * unchanged for as long as the linked JPX is used. */
typedef struct {
    const char *path;       /* for messages, and for links */
    const uint8_t *buf;     /* the whole file */
    size_t size;
} hv_merge_input;

/* The inputs, opened when needed: open fills *in for input i, whose path
 * stays valid until hv_merge_files returns and whose bytes stay valid until
 * close; 0, or -1 with a message in error. hv_merge_files opens the first
 * input once, checks it and keeps it open. It opens every later input twice.
 * The first pass checks hv_check_jp2 and hv_read_jp2h, plus the complete
 * codestream with HV_PROFILE when validate is nonzero. The second pass only
 * reacquires the same immutable bytes and uses the recorded layout to copy
 * header, IPR and XML boxes, and the codestream unless linked. At most two
 * inputs are open at a time. */
typedef struct {
    int (*open)(void *context, size_t i, hv_merge_input *in, char *error, size_t error_size);
    void (*close)(void *context, size_t i, hv_merge_input *in);
    void *context;
} hv_merge_inputs;

/* Writes the JPX file merging n inputs to out: their codestreams embedded,
 * or with `links`, linked. `validate` adds full codestream validation.
 * 0, or -1 with a message in error; out may then
 * hold part of the file. */
int hv_merge_files(const hv_merge_inputs *inputs, size_t n, int links, int validate, FILE *out,
                   char *error, size_t error_size);

/* The same, for inputs already in memory. */
int hv_merge_buffers(const hv_merge_input *inputs, size_t n, int links, int validate, FILE *out,
                     char *error, size_t error_size);

#endif
