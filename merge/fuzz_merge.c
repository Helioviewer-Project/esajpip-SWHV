/* fuzz_merge: a libFuzzer target for hv_merge_files. The input is two JP2
 * files: a 4-byte big-endian length L, the first L bytes (modulo what
 * remains), then the second file. They are merged in that order, embedded,
 * so an input with a palette and one without exercise the generated cmap.
 * An accepted merge must be a JPX file the server serves (hv_check_jpx,
 * every codestream with HV_PROFILE) with valid header boxes
 * (hv_check_jpx_headers).
 *
 *   cmake -S . -B fuzz -DCMAKE_C_COMPILER=clang -DESAJPIP_SANITIZE=ON -DESAJPIP_FUZZ=ON
 *   cmake --build fuzz --target fuzz_merge
 *   fuzz/merge/fuzz_merge -max_len=262144 corpus/ */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "hv_reader.h"
#include "merge.h"

/* The merged file, read back from the stream. */
static uint8_t *read_back(FILE *f, size_t *size) {
    long n;
    uint8_t *buf;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        (buf = malloc(n ? (size_t)n : 1)) == NULL)
        return NULL;
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        return NULL;
    }
    *size = (size_t)n;
    return buf;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    hv_merge_input in[2] = {{"first.jp2", NULL, 0}, {"second.jp2", NULL, 0}};
    char error[512];
    FILE *f;
    uint8_t *jpx;
    size_t n, at, i;
    hv_jpx j;

    if (size < 4)
        return 0;
    n = ((size_t)data[0] << 24 | (size_t)data[1] << 16 | (size_t)data[2] << 8 | data[3]) %
        (size - 4 + 1);
    in[0].buf = data + 4;
    in[0].size = n;
    in[1].buf = data + 4 + n;
    in[1].size = size - 4 - n;
    if ((f = tmpfile()) == NULL)
        return 0;
    if (hv_merge_files(in, 2, 0, f, error, sizeof error) == 0) {
        if ((jpx = read_back(f, &n)) == NULL || hv_check_jpx_headers(jpx, n, &at) != NULL ||
            hv_check_jpx(jpx, n, &j, &at) != NULL)
            abort();
        for (i = 0; i < j.count; i++)
            if (hv_codestream_check(jpx, j.jp2c[i].payload, j.jp2c[i].end, HV_PROFILE, &at) != NULL)
                abort();
        hv_jpx_free(&j);
        free(jpx);
    }
    fclose(f);
    return 0;
}
