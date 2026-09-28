/* Decodes Outer from a 3-byte buffer whose length byte says 10: the
 * region runs past the buffer. The decoder must fail without reading
 * beyond the 3 bytes; build with -fsanitize=address to see the read.
 * Exit status 0 means it failed cleanly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "min.h"

int main(void) {
    unsigned char *buf = malloc(3);      /* exactly the input: no slack */
    BitStream s;
    Outer v;
    int err = 0;
    flag ok;
    buf[0] = 10;                         /* len: 10 bytes of body */
    buf[1] = 0xAA;
    buf[2] = 0xBB;                       /* but only 2 follow */
    BitStream_AttachBuffer(&s, buf, 3);
    ok = Outer_ACN_Decode(&v, &s, &err);
    printf("decode %s (err %d)\n", ok ? "succeeded" : "failed", err);
    free(buf);
    return ok ? 1 : 0;
}
