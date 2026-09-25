/* fuzz_transcode: a libFuzzer target for hv_transcode_codestream. Every
 * input is transcoded with 128x128 precincts; an accepted input's output
 * must be accepted again and transcode to itself.
 *
 *   cmake -DCMAKE_C_COMPILER=clang -DESAJPIP_SANITIZE=ON -DESAJPIP_FUZZ=ON ..
 *   make fuzz_transcode && ./transcode/fuzz_transcode corpus/ */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "transcode.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    hv_out once, twice;
    char error[256];

    hv_out_init(&once);
    hv_out_init(&twice);
    if (hv_transcode_codestream(data, 0, size, 7, 7, &once, error, sizeof error) == 0) {
        if (hv_transcode_codestream(once.data, 0, once.size, 7, 7, &twice, error,
                                    sizeof error) != 0 ||
            twice.size != once.size || memcmp(twice.data, once.data, once.size) != 0)
            abort();
    }
    hv_out_free(&once);
    hv_out_free(&twice);
    return 0;
}
