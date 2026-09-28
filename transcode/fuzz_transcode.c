/* fuzz_transcode: a libFuzzer target for hv_transcode_codestream. Every
 * input is transcoded with 128x128 precincts; an accepted input's output
 * must be accepted again and transcode to itself, and if its main header
 * is within the served profile, so must the rest (HV_PROFILE).
 *
 *   cmake -S . -B fuzz -DCMAKE_C_COMPILER=clang -DESAJPIP_SANITIZE=ON -DESAJPIP_FUZZ=ON
 *   cmake --build fuzz --target fuzz_transcode
 *   fuzz/transcode/fuzz_transcode -max_len=131072 corpus/ */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "transcode.h"

/* Nonzero if the codestream reads through with these flags. */
static int reads(const hv_out *cs, unsigned flags) {
    size_t at;
    return hv_codestream_check(cs->data, 0, cs->size, flags, &at) == NULL;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    hv_out once, twice;
    char error[256];

    hv_out_init(&once);
    hv_out_init(&twice);
    if (hv_transcode_codestream(data, 0, size, 7, 7, 0, &once, error, sizeof error) == 0) {
        if (hv_transcode_codestream(once.data, 0, once.size, 7, 7, 0, &twice, error,
                                    sizeof error) != 0 ||
            twice.size != once.size || memcmp(twice.data, once.data, once.size) != 0)
            abort();
        if (reads(&once, HV_PROFILE_HEADERS) && !reads(&once, HV_PROFILE))
            abort();
    }
    hv_out_free(&once);
    hv_out_free(&twice);
    return 0;
}
