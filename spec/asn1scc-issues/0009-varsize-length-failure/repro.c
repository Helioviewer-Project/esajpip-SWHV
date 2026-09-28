/* Decodes Mask and Bits, whose 4-bit length is at most 8, from inputs whose
 * length does not decode: 15 (0xF0), and no input at all. Every decode must
 * fail with a nonzero error code. Built with UBSan, which reports the
 * decoder's use of the empty stream's null buffer. Exit status 0: all fail. */
#include <stdio.h>

#include "min.h"

typedef flag (*decoder)(void *value, BitStream *stream, int *error);

static int check(const char *what, decoder decode, void *value, unsigned char *buffer, long size) {
    BitStream stream;
    int error = 0;
    flag decoded;
    BitStream_AttachBuffer(&stream, size > 0 ? buffer : NULL, size);
    decoded = decode(value, &stream, &error);
    printf("%s, %ld bytes: decoded=%d error=%d\n", what, size, decoded, error);
    return !decoded && error != 0;
}

static flag mask_uper(void *v, BitStream *s, int *e) { return Mask_Decode(v, s, e); }
static flag mask_acn(void *v, BitStream *s, int *e) { return Mask_ACN_Decode(v, s, e); }
static flag bits_uper(void *v, BitStream *s, int *e) { return Bits_Decode(v, s, e); }
static flag bits_acn(void *v, BitStream *s, int *e) { return Bits_ACN_Decode(v, s, e); }

int main(void) {
    unsigned char buffer[1] = {0xF0};
    Mask mask;
    Bits bits;
    int passed = 1;
    long size;
    for (size = 1; size >= 0; size--) {
        passed &= check("Mask uPER", mask_uper, &mask, buffer, size);
        passed &= check("Mask ACN", mask_acn, &mask, buffer, size);
        passed &= check("Bits uPER", bits_uper, &bits, buffer, size);
        passed &= check("Bits ACN", bits_acn, &bits, buffer, size);
    }
    return passed ? 0 : 1;
}
