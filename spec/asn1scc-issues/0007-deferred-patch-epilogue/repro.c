/* Encodes Chain values after one byte FF already in the stream and
 * compares the bytes and the error code with the expected ones. Exit
 * status 0 means every case is right. */
#include <stdio.h>
#include <string.h>
#include "min.h"

static int run(const char *name, const Chain *v, const unsigned char *want, long nwant) {
    unsigned char buf[16];
    BitStream s;
    int err = 0, ok, right;
    long i, n;

    memset(buf, 0, sizeof buf);
    BitStream_AttachBuffer(&s, buf, sizeof buf);
    BitStream_EncodeConstraintWholeNumber(&s, 0xFF, 0, 255);
    ok = Chain_ACN_Encode(v, &s, &err, TRUE);
    n = BitStream_GetLength(&s);
    right = ok && err == 0 && n == nwant && memcmp(buf, want, (size_t)n) == 0;
    printf("%s: %s ret=%d err=%d bytes:", right ? "ok  " : "BAD ", name, ok, err);
    for (i = 0; i < n; i++) printf(" %02X", buf[i]);
    printf("   expected ret=1 err=0 bytes:");
    for (i = 0; i < nwant; i++) printf(" %02X", want[i]);
    printf("\n");
    return right;
}

int main(void) {
    static const unsigned char one[] = {0xFF, 0x05};
    static const unsigned char two[] = {0xFF, 0x81, 0x48};
    static const unsigned char four[] = {0xFF, 0x81, 0x80, 0x80, 0x05};
    Chain v;
    int all = 1;

    memset(&v, 0, sizeof v);                 /* 1 byte: b0 */
    v.b0.bits = 5;
    all &= run("b0", &v, one, sizeof one);

    memset(&v, 0, sizeof v);                 /* 2 bytes: b0 b1 */
    v.b0.bits = 1;
    v.exist.b1 = 1;
    v.b1.bits = 0x48;
    all &= run("b0 b1", &v, two, sizeof two);

    memset(&v, 0, sizeof v);                 /* 4 bytes: b0 b1 b2 b3 */
    v.b0.bits = 1;
    v.exist.b1 = 1;
    v.exist.b2 = 1;
    v.exist.b3 = 1;
    v.b3.bits = 5;
    all &= run("b0 b1 b2 b3", &v, four, sizeof four);

    return all ? 0 : 1;
}
