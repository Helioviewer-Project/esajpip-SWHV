/* hv_decode.h: the generated decoders on a bounded window, as the reader,
 * hv_rewrite and hv_merge run them. A decoder gets exactly `size` bytes
 * at buf + pos, so it can never read past the enclosing box, segment or
 * codestream; HV_DECODE_USED also gives the bytes it read, so that a
 * field's position comes from the model's layout, never from the caller's
 * arithmetic. Include it in a .c file, and write HV_DEFINE_DECODE(T) there
 * for each type decoded. */
#ifndef HV_DECODE_H
#define HV_DECODE_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "asn1crt_encoding.h"

/* hv_decode_T(value, buf, pos, size, used): 0, or -1 when T does not
 * decode from the size bytes at buf + pos; *used (unless NULL) the bytes
 * it read. With size 0, buf is not used (it may be NULL, as for an empty
 * file). */
#define HV_DEFINE_DECODE(T)                                                  \
    static int hv_decode_##T(T *value, const uint8_t *buf, size_t pos,      \
                             size_t size, size_t *used) {                    \
        BitStream s;                                                         \
        int err = 0;                                                         \
        if (size > (size_t)LONG_MAX) size = (size_t)LONG_MAX;               \
        BitStream_AttachBuffer(&s, size ? (unsigned char *)(buf + pos) : NULL, \
                               (long)size);                                  \
        if (!T##_ACN_Decode(value, &s, &err))                               \
            return -1;                                                       \
        if (used != NULL)                                                    \
            *used = (size_t)BitStream_GetLength(&s);                         \
        return 0;                                                            \
    }

#define HV_DECODE(T, value, buf, pos, size)                                  \
    hv_decode_##T((value), (buf), (pos), (size), NULL)
#define HV_DECODE_USED(T, value, buf, pos, size, used)                       \
    hv_decode_##T((value), (buf), (pos), (size), (used))

#endif
