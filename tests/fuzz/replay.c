/* replay: the deterministic counterpart of the fuzz targets, for sanitizer,
 * coverage and Valgrind runs that cannot use libFuzzer.
 *
 * Each target is included here under another name, so a mode *is* its target
 * and cannot drift from it:
 *
 *   reader-rewrite   fuzz_reader_rewrite   the reader and hv_rewrite
 *   deferred-plt     fuzz_deferred_plt     deferred PLT consumption
 *   asn1             fuzz_asn1             the generated decoders
 *   transcode-fuzz   fuzz_transcode        codestream transcode
 *   merge-fuzz       fuzz_merge            merging, through a temporary file
 *
 * Usage: replay MODE FILE_OR_DIR...
 *
 * With ESAJPIP_REPLAY_VERBOSE=1 (or -v as the second word) each input is
 * named before it runs, so a crash under a corpus names its file. */

/* Before any header, for the targets included below (fuzz_merge's mkdtemp
 * and realpath). */
#define _XOPEN_SOURCE 700
#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "hv_served.h"

#define LLVMFuzzerTestOneInput replay_reader_rewrite
#include "fuzz_reader_rewrite.c"
#undef LLVMFuzzerTestOneInput

#define LLVMFuzzerTestOneInput replay_deferred_plt
#include "fuzz_deferred_plt.c"
#undef LLVMFuzzerTestOneInput

#define LLVMFuzzerTestOneInput replay_asn1
#include "fuzz_asn1.c"
#undef LLVMFuzzerTestOneInput

#define LLVMFuzzerTestOneInput replay_transcode_fuzz
#include "fuzz_transcode.c"
#undef LLVMFuzzerTestOneInput

#define LLVMFuzzerTestOneInput replay_merge_fuzz
#include "fuzz_merge.c"
#undef LLVMFuzzerTestOneInput

typedef int (*replay_fn)(const uint8_t *data, size_t size);

static replay_fn mode_function(const char *mode) {
    if (strcmp(mode, "reader-rewrite") == 0)
        return replay_reader_rewrite;
    if (strcmp(mode, "deferred-plt") == 0)
        return replay_deferred_plt;
    if (strcmp(mode, "asn1") == 0)
        return replay_asn1;
    if (strcmp(mode, "transcode-fuzz") == 0)
        return replay_transcode_fuzz;
    if (strcmp(mode, "merge-fuzz") == 0)
        return replay_merge_fuzz;
    return NULL;
}

static void usage(const char *program) {
    fprintf(stderr,
            "usage: %s MODE FILE_OR_DIR...\n"
            "modes: reader-rewrite deferred-plt asn1 transcode-fuzz merge-fuzz\n",
            program);
}

static uint8_t *load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;

    if (f == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: cannot size\n", path);
        fclose(f);
        return NULL;
    }
    buf = malloc(n > 0 ? (size_t)n : 1);
    if (buf == NULL || (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n)) {
        fprintf(stderr, "%s: cannot read\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static int compare_strings(const void *a, const void *b) {
    const char *const *left = a;
    const char *const *right = b;
    return strcmp(*left, *right);
}

static void free_paths(char **paths, size_t count) {
    size_t i;
    for (i = 0; i < count; i++)
        free(paths[i]);
    free(paths);
}

static char *join_path(const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    char *path = malloc(dir_len + 1 + name_len + 1);

    if (path == NULL)
        return NULL;
    memcpy(path, dir, dir_len);
    path[dir_len] = '/';
    memcpy(path + dir_len + 1, name, name_len + 1);
    return path;
}

static int run_one(replay_fn replay, const char *path, int verbose) {
    size_t size = 0;
    uint8_t *data = load(path, &size);
    int failed = 0;

    if (data == NULL)
        return 1;
    if (verbose) {
        printf("run %s (%zu bytes)\n", path, size);
        fflush(stdout);
    }
    if (replay(data, size) != 0)
        failed = 1;
    free(data);
    return failed;
}

static int run_directory(replay_fn replay, const char *path, int verbose) {
    DIR *dir = opendir(path);
    struct dirent *entry;
    char **paths = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t i;
    int failed = 0;

    if (dir == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return 1;
    }
    while ((entry = readdir(dir)) != NULL) {
        char **next;
        char *joined;

        if (entry->d_name[0] == '.')
            continue;
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 32;
            next = realloc(paths, capacity * sizeof *paths);
            if (next == NULL) {
                free_paths(paths, count);
                closedir(dir);
                return 1;
            }
            paths = next;
        }
        joined = join_path(path, entry->d_name);
        if (joined == NULL) {
            free_paths(paths, count);
            closedir(dir);
            return 1;
        }
        paths[count++] = joined;
    }
    closedir(dir);

    qsort(paths, count, sizeof *paths, compare_strings);
    for (i = 0; i < count; i++)
        failed |= run_one(replay, paths[i], verbose);
    free_paths(paths, count);
    return failed;
}

static int run_path(replay_fn replay, const char *path, int verbose) {
    struct stat st;

    if (stat(path, &st) != 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return 1;
    }
    if (S_ISDIR(st.st_mode))
        return run_directory(replay, path, verbose);
    return run_one(replay, path, verbose);
}

int main(int argc, char **argv) {
    replay_fn replay;
    int verbose = getenv("ESAJPIP_REPLAY_VERBOSE") != NULL;
    int failed = 0;
    int i = 2;

    if (argc >= 3 && strcmp(argv[2], "-v") == 0) {
        verbose = 1;
        i = 3;
    }
    if (argc < 3 || (replay = mode_function(argv[1])) == NULL) {
        usage(argv[0]);
        return 2;
    }
    for (; i < argc; i++) {
        if (run_path(replay, argv[i], verbose) != 0)
            failed = 1;
    }
    return failed ? 1 : 0;
}
