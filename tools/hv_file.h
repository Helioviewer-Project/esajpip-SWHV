/* hv_file.h: replaces an output file safely, for hv_transcode and hv_merge.
 *
 * The new contents go to a temporary file next to the output, in the same
 * directory, which is renamed over the output once complete. So a failed
 * write leaves the output as it was, and the output may be one of the
 * inputs. An output that is a symbolic link is resolved first, as opening
 * it for writing would: the temporary goes next to the file it points to,
 * which is replaced or, for a link to no file yet, created, and the link
 * stays. Error messages name the output as given, never the temporary.
 *
 * Replacement is atomic; crash durability is not guaranteed.
 * While the temporary file exists,
 * SIGINT, SIGTERM, SIGHUP and SIGQUIT remove it before they take their
 * course (the handler that was there, or the default); a signal the
 * process ignores (nohup's SIGHUP) stays ignored. So does SIGBUS, which a
 * mapped input that another process truncated raises when the lost bytes
 * are read: it also writes "SIGBUS (an input file changed while it was
 * read?); the output is left as it was" to standard error.
 *
 * A replaced file's owner and group are given to the new file where the
 * process may (its group alone, else neither: an administrator's run
 * keeps a service account's files theirs). Its ACLs and extended
 * attributes are not kept.
 *
 * Not thread-safe, and one hv_file at a time: reading the umask sets it
 * for a moment, and the signal handlers know one temporary file, so
 * hv_file_create fails while another hv_file is open. */
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

/* Flushes and closes the temporary file, then renames it over the output.
 * 0, or -1 with a message in error and the temporary file removed.
 * Either way f is finished. */
int hv_file_commit(hv_file *f, char *error, size_t error_size);

/* Closes and removes the temporary file: the output stays as it was. */
void hv_file_abort(hv_file *f);

#ifdef __cplusplus
}
#endif

#endif
