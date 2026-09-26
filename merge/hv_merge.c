/* hv_merge: merges JP2 files into a JPX file for JHelioviewer and the
 * JPIP server, as hvJP2K's hv_jpx_merge does (see merge.h), with the same
 * command line:
 *
 *   hv_merge -i jp2file1,jp2file2,... [jp2file ...] -o jpxfile [-links]
 *                [-s argfile]
 *
 *   -i      input JP2 files, comma or space separated, in the JPX order
 *   -o      output JPX file
 *   -links  link the codestreams instead of copying them
 *   -s      read more arguments from a file (split as the shell would,
 *           quotes and backslashes included)
 *
 * The output is written to a temporary file next to it and renamed into
 * place. Exit status: 0 on success, 1 on error, 2 on usage errors. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "merge.h"

typedef struct {
    char **v;
    size_t n, cap;
} list;

static int push(list *l, char *s) {
    if (l->n == l->cap) {
        size_t cap = l->cap ? 2 * l->cap : 64;
        char **v = realloc(l->v, cap * sizeof *v);
        if (v == NULL)
            return -1;
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = s;
    return 0;
}

static void usage(void) {
    fprintf(stderr, "usage: hv_merge -i jp2file1,jp2file2,... [jp2file ...] -o jpxfile "
                    "[-links] [-s argfile]\n");
}

/* The words of `text`, split as Python's shlex.split does: whitespace
 * separates them, backslash escapes any character outside quotes, single
 * quotes keep everything, double quotes let backslash escape only a double
 * quote or a backslash. Words point into `text`, which is rewritten. -1 on
 * an unterminated quote or a final backslash. */
static int split_words(char *text, list *words) {
    char *r = text, *w = text;
    while (*r) {
        char *word;
        while (*r == ' ' || *r == '\t' || *r == '\n' || *r == '\r' || *r == '\v' || *r == '\f')
            r++;
        if (!*r)
            break;
        word = w;
        while (*r && !(*r == ' ' || *r == '\t' || *r == '\n' || *r == '\r' || *r == '\v' ||
                       *r == '\f')) {
            if (*r == '\\') {
                if (!r[1])
                    return -1;
                *w++ = r[1];
                r += 2;
            } else if (*r == '\'') {
                for (r++; *r && *r != '\''; )
                    *w++ = *r++;
                if (!*r++)
                    return -1;
            } else if (*r == '"') {
                for (r++; *r && *r != '"'; ) {
                    if (*r == '\\' && (r[1] == '"' || r[1] == '\\'))
                        r++;
                    *w++ = *r++;
                }
                if (!*r++)
                    return -1;
            } else {
                *w++ = *r++;
            }
        }
        if (*r)
            r++;                                 /* the separator */
        *w++ = 0;
        if (push(words, word) != 0)
            return -1;
    }
    return 0;
}

/* The whole of a file as a string, or NULL. */
static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    char *text = NULL;
    size_t n = 0, cap = 0, got;
    if (f == NULL)
        return NULL;
    do {
        if (n + 4096 + 1 > cap) {
            char *t = realloc(text, cap = 2 * cap + 4096 + 1);
            if (t == NULL) {
                free(text);
                fclose(f);
                return NULL;
            }
            text = t;
        }
        n += got = fread(text + n, 1, 4096, f);
    } while (got > 0);
    fclose(f);
    text[n] = 0;
    return text;
}

typedef struct {
    list inputs;            /* the -i words */
    const char *output, *argfile;
    int links;
} arguments;

/* Parses argv as argparse does for hvJP2K's options: -i takes the words up
 * to the next option (the last -i counts). 0, or -1 on a usage error. */
static int parse(char **argv, size_t argc, arguments *a) {
    size_t i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            a->inputs.n = 0;
            while (i + 1 < argc && argv[i + 1][0] != '-')
                if (push(&a->inputs, argv[++i]) != 0)
                    return -1;
            if (a->inputs.n == 0)
                return -1;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc && argv[i + 1][0] != '-') {
            a->output = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc && argv[i + 1][0] != '-') {
            a->argfile = argv[++i];
        } else if (strcmp(argv[i], "-links") == 0) {
            a->links = 1;
        } else {
            return -1;
        }
    }
    return 0;
}

/* The mapped inputs, and the JPX written through a temporary file. */
static int merge(char **names, size_t n, const char *output, int links, char *error,
                 size_t error_size) {
    hv_merge_input *in = calloc(n ? n : 1, sizeof *in);
    size_t i, len = strlen(output);
    char *tmp = malloc(len + 8);
    FILE *file = NULL;
    mode_t mask = umask(0);
    int fd = -1, status = -1;

    umask(mask);
    if (in == NULL || tmp == NULL) {
        snprintf(error, error_size, "out of memory");
        goto done;
    }
    for (i = 0; i < n; i++) {
        struct stat st;
        in[i].path = names[i];
        in[i].buf = NULL;
        if ((fd = open(names[i], O_RDONLY)) < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
            snprintf(error, error_size, "cannot open %s: %s", names[i],
                     fd < 0 || errno ? strerror(errno) : "not a regular file");
            goto done;
        }
        in[i].size = (size_t)st.st_size;
        if (in[i].size != 0) {
            void *p = mmap(NULL, in[i].size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (p == MAP_FAILED) {
                snprintf(error, error_size, "cannot map %s: %s", names[i], strerror(errno));
                goto done;
            }
            in[i].buf = p;
        } else {
            in[i].buf = (const uint8_t *)"";
        }
        close(fd);
        fd = -1;
    }
    memcpy(tmp, output, len);
    memcpy(tmp + len, ".XXXXXX", 8);
    if ((fd = mkstemp(tmp)) < 0) {
        snprintf(error, error_size, "cannot create %s: %s", tmp, strerror(errno));
        goto done;
    }
    if (fchmod(fd, 0666 & ~mask) != 0 || (file = fdopen(fd, "wb")) == NULL) {
        snprintf(error, error_size, "cannot write %s: %s", tmp, strerror(errno));
        close(fd);
        unlink(tmp);
        goto done;
    }
    if (hv_merge_files(in, n, links, file, error, error_size) != 0) {
        fclose(file);
        unlink(tmp);
        goto done;
    }
    if (fclose(file) != 0 || rename(tmp, output) != 0) {
        snprintf(error, error_size, "cannot write %s: %s", output, strerror(errno));
        unlink(tmp);
        goto done;
    }
    status = 0;

done:
    for (i = 0; in != NULL && i < n; i++)
        if (in[i].buf != NULL && in[i].size != 0)
            munmap((void *)in[i].buf, in[i].size);
    if (fd >= 0 && file == NULL)
        close(fd);
    free(in);
    free(tmp);
    return status;
}

int main(int argc, char **argv) {
    arguments a;
    list names = {NULL, 0, 0}, extra = {NULL, 0, 0}, all = {NULL, 0, 0};
    char *text = NULL, error[4096];
    size_t i;
    int status = 2;

    memset(&a, 0, sizeof a);
    if (parse(argv + 1, (size_t)argc - 1, &a) != 0)
        goto usage;
    if (a.argfile != NULL) {
        /* argparse's reparse: the command line, then the file's words. */
        if ((text = read_text(a.argfile)) == NULL || split_words(text, &extra) != 0) {
            fprintf(stderr, "hv_merge: cannot read the arguments in %s\n", a.argfile);
            status = 1;
            goto done;
        }
        for (i = 1; i < (size_t)argc; i++)
            if (push(&all, argv[i]) != 0)
                goto oom;
        for (i = 0; i < extra.n; i++)
            if (push(&all, extra.v[i]) != 0)
                goto oom;
        free(a.inputs.v);
        memset(&a, 0, sizeof a);
        if (parse(all.v, all.n, &a) != 0)
            goto usage;
    }
    /* Names are split at commas, empty ones dropped (hvJP2K is lenient
     * with stray commas). */
    for (i = 0; i < a.inputs.n; i++) {
        char *p = a.inputs.v[i], *comma;
        for (;;) {
            if ((comma = strchr(p, ',')) != NULL)
                *comma = 0;
            if (*p && push(&names, p) != 0)
                goto oom;
            if (comma == NULL)
                break;
            p = comma + 1;
        }
    }
    if (names.n == 0 || a.output == NULL)
        goto usage;
    status = merge(names.v, names.n, a.output, a.links, error, sizeof error) != 0;
    if (status != 0)
        fprintf(stderr, "hv_merge: %s\n", error);
    goto done;

oom:
    fprintf(stderr, "hv_merge: out of memory\n");
    status = 1;
    goto done;
usage:
    usage();
done:
    free(names.v);
    free(extra.v);
    free(all.v);
    free(a.inputs.v);
    free(text);
    return status;
}
