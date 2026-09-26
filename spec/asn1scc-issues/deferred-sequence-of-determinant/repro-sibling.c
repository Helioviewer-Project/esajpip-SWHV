/* Encodes sibling.asn1's Msg twice: head and payload.data of 2 bytes each
 * (len = 2), then a head of 3 bytes, which no single len can size with
 * data of 2 and the encoder must reject. */
#include <stdio.h>
#include <string.h>

#include "sibling.h"

static int encode(const Msg *msg, byte *buf, long size, long *n) {
    BitStream bs;
    int err = 0;
    flag ok;
    BitStream_Init(&bs, buf, size);
    ok = Msg_ACN_Encode(msg, &bs, &err, TRUE);
    *n = BitStream_GetLength(&bs);
    return ok;
}

int main(void) {
    static byte buf[16];
    BitStream bs;
    Msg msg, back;
    int err = 0, i, ok, rejected;
    long n;

    memset(&msg, 0, sizeof msg);
    msg.head.nCount = 2;
    memset(msg.head.arr, 0xAA, 2);
    msg.payload.data.nCount = 2;
    memset(msg.payload.data.arr, 0xBB, 2);
    ok = encode(&msg, buf, sizeof buf, &n);
    printf("encode ok=%d:", ok);
    for (i = 0; ok && i < n; i++)
        printf(" %02X", buf[i]);
    printf("\n");
    BitStream_AttachBuffer(&bs, buf, n);
    ok = ok && Msg_ACN_Decode(&back, &bs, &err) && back.head.nCount == 2 &&
         back.payload.data.nCount == 2;
    printf("decode ok=%d\n", ok);
    msg.head.nCount = 3;
    rejected = !encode(&msg, buf, sizeof buf, &n);
    printf("head 3, data 2: encode %s\n", rejected ? "rejected" : "accepted");
    return ok && rejected ? 0 : 1;
}
