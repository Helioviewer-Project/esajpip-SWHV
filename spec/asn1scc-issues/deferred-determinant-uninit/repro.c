/* Decodes Msg from truncated input. Reading the deferred determinant
 * a.more fails, and the generated decoder then copies Msg_a_more_tmp,
 * which the failed read never wrote. dirty_stack() only makes that
 * indeterminate value visible to UBSan's bool check deterministically. */
#include <stdio.h>
#include <string.h>

#include "min.h"

static void dirty_stack(void) {
    volatile unsigned char junk[4096];
    memset((void *)junk, 0x4F, sizeof junk);
}

static int decode(const byte *data, long size) {
    static byte copy[2];
    BitStream bs;
    Msg msg;
    int err = 0;
    flag ok;

    memcpy(copy, data, (size_t)size);
    BitStream_AttachBuffer(&bs, copy, size);
    dirty_stack();
    ok = Msg_ACN_Decode(&msg, &bs, &err);
    printf("%ld byte(s): decode ok=%d err=%d\n", size, ok, err);
    return ok;
}

int main(void) {
    static const byte more[1] = {0x80};   /* a.more = 1, a.bits = 0 */

    decode(more, 0);                      /* a.more itself missing */
    return 0;
}
