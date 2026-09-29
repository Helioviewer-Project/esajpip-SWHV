/* fuzz_reader_rewrite: container reader and writer stability for JP2/JPX
 * structures. Successful rewrites must be readable again. Byte-for-byte
 * comparable rewrites must not change the input, and normalized rewrites
 * must be stable after one pass.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "jpeg2000/hv_reader.h"
#include "jpeg2000/hv_rewrite.h"

static void check_rewrite_stability(const uint8_t *data, size_t size) {
    hv_out once, twice;
    hv_rewrite_result first, second;

    hv_out_init(&once);
    hv_out_init(&twice);
    memset(&first, 0, sizeof first);
    memset(&second, 0, sizeof second);

    if (hv_rewrite(data, size, 0, 0, &once, &first) == 0) {
        if (first.uncomparable == NULL &&
            (once.size != size || (size != 0 && memcmp(once.data, data, size) != 0)))
            abort();
        if (hv_rewrite(once.data, once.size, 0, 0, &twice, &second) != 0)
            abort();
        if (twice.size != once.size ||
            (once.size != 0 && memcmp(twice.data, once.data, once.size) != 0))
            abort();
    }

    hv_out_free(&twice);
    hv_out_free(&once);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    hv_box jp2c;
    hv_jpx jpx;
    size_t at = 0;

    check_rewrite_stability(data, size);
    if (hv_check_jp2(data, size, &jp2c, &at) == NULL) {
        (void)hv_check_jp2h(data, size, &at);
        (void)hv_codestream_check(data, jp2c.payload, jp2c.end, HV_PROFILE, &at);
    }
    if (hv_check_jpx(data, size, &jpx, &at) == NULL) {
        (void)hv_check_jpx_headers(data, size, &at);
        for (size_t i = 0; i < jpx.count && jpx.jp2c != NULL; i++)
            (void)hv_codestream_check(data, jpx.jp2c[i].payload, jpx.jp2c[i].end,
                                      HV_PROFILE, &at);
        hv_jpx_free(&jpx);
    }
    return 0;
}
