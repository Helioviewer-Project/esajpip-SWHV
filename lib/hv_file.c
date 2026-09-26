/* hv_file.c: see hv_file.h. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "hv_file.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hv_error.h"

/* The suffix mkstemp replaces. */
static const char suffix[] = ".XXXXXX";

static void clear(hv_file *f) {
    free(f->tmp);
    free(f->target);
    f->tmp = f->target = NULL;
    f->file = NULL;
    f->fd = -1;
}

/* Closes and removes the temporary file, keeping errno. */
static void discard(hv_file *f) {
    int saved = errno;
    if (f->file != NULL)
        fclose(f->file);
    else if (f->fd >= 0)
        close(f->fd);
    if (f->tmp != NULL)
        unlink(f->tmp);
    clear(f);
    errno = saved;
}

/* The file that opening `path` for writing would write: path with its
 * symbolic links resolved (realpath), or, where it does not exist yet, the
 * path itself or the one its last link names, as open follows a link to a
 * file it creates. NULL with errno set: ELOOP for a link cycle. */
static char *resolve(const char *path) {
    char *p = strdup(path);
    int links;
    for (links = 0; p != NULL; links++) {
        char *real = realpath(p, NULL), *target = NULL, *next;
        const char *slash;
        struct stat st;
        size_t size = 256, dir;
        ssize_t n = 0;
        if (real != NULL || lstat(p, &st) != 0 || !S_ISLNK(st.st_mode)) {
            if (real == NULL)
                return p;                   /* a file to be created */
            free(p);
            return real;
        }
        if (links == 40) {                  /* as Linux's MAXSYMLINKS */
            free(p);
            errno = ELOOP;
            return NULL;
        }
        do {
            char *grown = realloc(target, size *= 2);
            if (grown == NULL) {
                free(target);
                free(p);
                errno = ENOMEM;
                return NULL;
            }
            target = grown;
        } while ((n = readlink(p, target, size)) >= 0 && (size_t)n == size);
        if (n < 0) {
            free(target);
            free(p);
            return NULL;
        }
        /* A relative target is relative to the link's directory. */
        slash = strrchr(p, '/');
        dir = target[0] == '/' || slash == NULL ? 0 : (size_t)(slash - p) + 1;
        if ((next = malloc(dir + (size_t)n + 1)) != NULL) {
            memcpy(next, p, dir);
            memcpy(next + dir, target, (size_t)n);
            next[dir + (size_t)n] = 0;
        } else {
            errno = ENOMEM;
        }
        free(target);
        free(p);
        p = next;
    }
    return NULL;
}

int hv_file_create(hv_file *f, const char *path, int mode, char *error, size_t error_size) {
    struct stat st;
    mode_t mask;
    size_t n;

    f->path = path;
    f->target = f->tmp = NULL;
    f->file = NULL;
    f->fd = -1;
    if (path[0] == 0)
        errno = ENOENT;                     /* as open gives */
    if (path[0] == 0 || (f->target = resolve(path)) == NULL)
        return hv_fail(error, error_size, "cannot create %s: %s", path, strerror(errno));
    if (mode == HV_FILE_KEEP_MODE) {
        mask = umask(0);
        umask(mask);
        mode = (int)(0666 & ~mask);
        if (stat(f->target, &st) == 0 && S_ISREG(st.st_mode))
            mode = (int)(st.st_mode & 0777);
    }
    n = strlen(f->target);
    if ((f->tmp = malloc(n + sizeof suffix)) == NULL) {
        clear(f);
        return hv_fail(error, error_size, "out of memory");
    }
    memcpy(f->tmp, f->target, n);
    memcpy(f->tmp + n, suffix, sizeof suffix);
    if ((f->fd = mkstemp(f->tmp)) < 0) {
        hv_fail(error, error_size, "cannot create %s: %s", path, strerror(errno));
        clear(f);               /* nothing was created */
        return -1;
    }
    if (fchmod(f->fd, (mode_t)(mode & 0777)) != 0 || (f->file = fdopen(f->fd, "wb")) == NULL) {
        discard(f);
        return hv_fail(error, error_size, "cannot write %s: %s", path, strerror(errno));
    }
    return 0;
}

int hv_file_write(hv_file *f, const void *bytes, size_t size, char *error, size_t error_size) {
    if (size != 0 && fwrite(bytes, 1, size, f->file) != size)
        return hv_fail(error, error_size, "cannot write %s: %s", f->path, strerror(errno));
    return 0;
}

int hv_file_commit(hv_file *f, char *error, size_t error_size) {
    int status = f->file != NULL ? fclose(f->file) : close(f->fd);
    f->file = NULL;
    f->fd = -1;
    if (status != 0 || rename(f->tmp, f->target) != 0) {
        discard(f);
        return hv_fail(error, error_size, "cannot write %s: %s", f->path, strerror(errno));
    }
    clear(f);
    return 0;
}

void hv_file_abort(hv_file *f) {
    discard(f);
}
