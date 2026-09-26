/* hv_file.h: replaces an output file safely, for hv_transcode and hv_merge.
 *
 * The new contents go to a temporary file next to the output, in the same
 * directory, which is renamed over the output once complete. So a failed
 * write leaves the output as it was, and the output may be one of the
 * inputs. An output that is a symbolic link is resolved first, as opening
 * it for writing would: the temporary goes next to the file it points to,
 * which is replaced or, for a link to no file yet, created, and the link
 * stays. Error messages name the output as given, never the temporary.
 * Not thread-safe: reading the umask sets it for a moment. */
#ifndef HV_FILE_H
#define HV_FILE_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *path;       /* the output as given, for messages */
    char *target;           /* the file replaced: path, symbolic links resolved */
    char *tmp;              /* the temporary file next to target */
    int fd;                 /* the temporary file open for writing */
    FILE *file;             /* the same, as a stream */
} hv_file;

/* The permissions hv_file_create gives the output when it keeps those of
 * an existing one. */
enum { HV_FILE_KEEP_MODE = -1 };

/* Creates the temporary file for `path` with permissions `mode` (0 to
 * 0777), or with HV_FILE_KEEP_MODE those of the existing output if it is
 * a regular file, else 0666 less the umask: what opening the output for
 * writing gives. Write through f->file, or through f->fd without using
 * f->file. 0, or -1 with a message in error; then there is nothing to
 * finish. */
int hv_file_create(hv_file *f, const char *path, int mode, char *error, size_t error_size);

/* Writes size bytes through f->file. 0, or -1 with a message in error. */
int hv_file_write(hv_file *f, const void *bytes, size_t size, char *error, size_t error_size);

/* Closes the temporary file and renames it over the output. 0, or -1 with
 * a message in error and the temporary file removed. Either way f is
 * finished. */
int hv_file_commit(hv_file *f, char *error, size_t error_size);

/* Closes and removes the temporary file: the output stays as it was. */
void hv_file_abort(hv_file *f);

#ifdef __cplusplus
}
#endif

#endif
