/* fuzz_asn1: the generated ACN decoders and encoders of every PDU type,
 * called directly rather than through the reader, so that determinants,
 * counts, optional fields and contained regions are tested where they are
 * generated.
 *
 * The first byte selects the type (modulo the number of types), the rest is
 * its encoding. asn1_pdus.h lists every type with a T_ACN_Decode, in the
 * order of the generated headers; tests/fuzz/CMakeLists.txt writes it from
 * jpeg2000/generated, so a type the model adds is fuzzed without an edit here.
 *
 * For each input, whatever the selected type:
 *   - T_Initialize's value, when it encodes (satisfying T_IsConstraintValid
 *     is not enough: an ACN present-when field it sets may contradict its
 *     determinant), round-trips as below;
 *   - a successful decode reads no more than the input, and its value
 *     satisfies T_IsConstraintValid and encodes, with and without the
 *     encoder's constraint check, to the same bytes;
 *   - that encoding fits T_REQUIRED_BYTES_FOR_ACN_ENCODING, decodes again
 *     reading exactly its own length, and re-encodes to the same bytes;
 *   - no strict prefix of it decodes by reading past the prefix;
 *   - every decode returns error code zero exactly when it succeeds.
 * Every buffer is allocated to its exact size, so that an access past it is
 * a sanitizer report rather than a read of a neighbouring byte.
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asn1crt_encoding.h"
#include "j2k-codestream.h"
#include "j2k-headers.h"
#include "jp2-boxes.h"
#include "jpeg2000-io.h"

/* A copy of n bytes in a buffer of exactly that size (one byte for n = 0,
 * never read). */
static unsigned char *asn1_copy(const uint8_t *data, size_t n) {
    unsigned char *copy = malloc(n ? n : 1);
    if (copy == NULL)
        abort();
    if (n)
        memcpy(copy, data, n);
    return copy;
}

static void asn1_attach(BitStream *s, unsigned char *buf, size_t n) {
    BitStream_AttachBuffer(s, n ? buf : NULL, n > (size_t)LONG_MAX ? LONG_MAX : (long)n);
}

/* check_T(data, n): the properties above for type T. round_T(value,
 * decoded) is the encode, decode, encode part; a value that was decoded must
 * encode, T_Initialize's may not. */
#define PDU(T)                                                                 \
    static flag decode_##T(T *value, BitStream *stream) {                     \
        int error = 0;                                                        \
        flag decoded = T##_ACN_Decode(value, stream, &error);                 \
        if (decoded ? error != 0 : error == 0) {                              \
            fprintf(stderr, #T ": decoded=%d error=%d\n", decoded, error);    \
            abort();                                                          \
        }                                                                     \
        return decoded;                                                       \
    }                                                                         \
    static void round_##T(const T *value, int decoded) {                       \
        const size_t cap = T##_REQUIRED_BYTES_FOR_ACN_ENCODING;                \
        unsigned char *once = malloc(cap ? cap : 1);                           \
        unsigned char *twice = malloc(cap ? cap : 1);                          \
        unsigned char *unchecked = malloc(cap ? cap : 1);                      \
        BitStream s;                                                           \
        T again;                                                               \
        int err = 0;                                                           \
        size_t n, k;                                                           \
        if (once == NULL || twice == NULL || unchecked == NULL)                \
            abort();                                                           \
        memset(once, 0, cap ? cap : 1);                                        \
        memset(twice, 0, cap ? cap : 1);                                       \
        memset(unchecked, 0, cap ? cap : 1);                                   \
        BitStream_Init(&s, once, (long)cap);                                   \
        if (!T##_ACN_Encode(value, &s, &err, TRUE)) {                          \
            if (decoded)                                                       \
                abort();                                                       \
            free(unchecked);                                                   \
            free(twice);                                                       \
            free(once);                                                        \
            return;                                                            \
        }                                                                      \
        n = (size_t)BitStream_GetLength(&s);                                   \
        if (n > cap)                                                           \
            abort();                                                           \
        BitStream_Init(&s, unchecked, (long)cap);                              \
        if (!T##_ACN_Encode(value, &s, &err, FALSE) ||                         \
            (size_t)BitStream_GetLength(&s) != n ||                            \
            (n && memcmp(unchecked, once, n) != 0))                            \
            abort();                                                           \
        {                                                                      \
            unsigned char *exact = asn1_copy(once, n);                         \
            T##_Initialize(&again);                                            \
            asn1_attach(&s, exact, n);                                         \
            if (!decode_##T(&again, &s) ||                                    \
                (size_t)BitStream_GetLength(&s) != n)                          \
                abort();                                                       \
            free(exact);                                                       \
        }                                                                      \
        BitStream_Init(&s, twice, (long)cap);                                  \
        if (!T##_ACN_Encode(&again, &s, &err, TRUE) ||                         \
            (size_t)BitStream_GetLength(&s) != n ||                            \
            (n && memcmp(twice, once, n) != 0))                                \
            abort();                                                           \
        for (k = 0; k < n; k++) {                                              \
            unsigned char *prefix = asn1_copy(once, k);                        \
            T##_Initialize(&again);                                            \
            asn1_attach(&s, prefix, k);                                        \
            if (decode_##T(&again, &s) &&                                     \
                (size_t)BitStream_GetLength(&s) > k)                           \
                abort();                                                       \
            free(prefix);                                                      \
        }                                                                      \
        free(unchecked);                                                       \
        free(twice);                                                           \
        free(once);                                                            \
    }                                                                          \
    static void check_##T(const uint8_t *data, size_t size) {                  \
        unsigned char *input = asn1_copy(data, size);                          \
        BitStream s;                                                           \
        T value;                                                               \
        int err = 0;                                                           \
        T##_Initialize(&value);                                                \
        if (T##_IsConstraintValid(&value, &err))                               \
            round_##T(&value, 0);                                              \
        T##_Initialize(&value);                                                \
        asn1_attach(&s, input, size);                                          \
        if (decode_##T(&value, &s)) {                                         \
            if ((size_t)BitStream_GetLength(&s) > size ||                      \
                !T##_IsConstraintValid(&value, &err))                          \
                abort();                                                       \
            round_##T(&value, 1);                                              \
        }                                                                      \
        free(input);                                                           \
    }
#include "asn1_pdus.h"
#undef PDU

typedef void (*asn1_check)(const uint8_t *data, size_t size);

static const asn1_check asn1_checks[] = {
#define PDU(T) check_##T,
#include "asn1_pdus.h"
#undef PDU
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const size_t types = sizeof asn1_checks / sizeof *asn1_checks;
    if (size == 0)
        return 0;
    asn1_checks[data[0] % types](data + 1, size - 1);
    return 0;
}
