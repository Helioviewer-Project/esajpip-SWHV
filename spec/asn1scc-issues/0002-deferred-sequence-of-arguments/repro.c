/* Encodes min.asn1's Msg: two items of 2 bytes, len = 2. The generated
 * encoder computes len from pVal->items.arr[i1] before the loop that sets
 * i1. dirty_stack() only makes that indeterminate index deterministic
 * (0x4F4F4F4F), so the read lands far outside items.arr. */
#include <stdio.h>
#include <string.h>

#include "min.h"

static void dirty_stack(void) {
    volatile unsigned char junk[4096];
    memset((void *)junk, 0x4F, sizeof junk);
}

int main(void) {
    static byte buf[16];
    BitStream bs;
    Msg msg, back;
    int err = 0, i;
    flag ok;
    long n;

    memset(&msg, 0, sizeof msg);
    msg.items.nCount = 2;
    for (i = 0; i < 2; i++) {
        msg.items.arr[i].data.nCount = 2;
        memset(msg.items.arr[i].data.arr, 0xA0 + i, 2);
    }
    BitStream_Init(&bs, buf, sizeof buf);
    dirty_stack();
    ok = Msg_ACN_Encode(&msg, &bs, &err, TRUE);
    n = BitStream_GetLength(&bs);
    printf("encode ok=%d:", ok);
    for (i = 0; ok && i < n; i++)
        printf(" %02X", buf[i]);
    printf("\n");
    BitStream_AttachBuffer(&bs, buf, n);
    ok = ok && Msg_ACN_Decode(&back, &bs, &err) && back.items.nCount == 2 &&
         back.items.arr[1].data.nCount == 2;
    printf("decode ok=%d\n", ok);
    return ok ? 0 : 1;
}
