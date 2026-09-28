/* fuzz_asn1: direct generated ASN.1 decoder/encoder exercise. These calls
 * intentionally bypass reader wrappers so malformed determinants, counts,
 * contained regions, and generated initialization contracts are tested at
 * their source.
 */
#include <stdint.h>
#include <limits.h>
#include <string.h>

#include "asn1crt_encoding.h"
#include "j2k-codestream.h"
#include "j2k-headers.h"
#include "jp2-boxes.h"
#include "jpeg2000-io.h"

#define TRY_TYPE(T)                                                           \
    do {                                                                      \
        T value;                                                              \
        T roundtrip;                                                          \
        BitStream input;                                                      \
        BitStream output;                                                     \
        int err = 0;                                                          \
        unsigned char encoded[T##_REQUIRED_BYTES_FOR_ACN_ENCODING + 32];      \
        T##_Initialize(&value);                                               \
        BitStream_AttachBuffer(&input, size ? (unsigned char *)data : NULL,   \
                               size > (size_t)LONG_MAX ? LONG_MAX : (long)size); \
        if (T##_ACN_Decode(&value, &input, &err)) {                           \
            memset(encoded, 0, sizeof encoded);                               \
            T##_Initialize(&roundtrip);                                       \
            BitStream_Init(&output, encoded, (long)sizeof encoded);           \
            err = 0;                                                          \
            if (T##_ACN_Encode(&value, &output, &err, TRUE)) {                \
                BitStream again;                                              \
                err = 0;                                                      \
                BitStream_AttachBuffer(&again, encoded,                       \
                                       BitStream_GetLength(&output));         \
                (void)T##_ACN_Decode(&roundtrip, &again, &err);               \
            }                                                                 \
        }                                                                     \
    } while (0)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    TRY_TYPE(BoxHeader);
    TRY_TYPE(RreqHeader);
    TRY_TYPE(RreqStandardFeature);
    TRY_TYPE(NlstEntry);
    TRY_TYPE(Fragment);
    TRY_TYPE(UrlHeader);
    TRY_TYPE(SizFixed);
    TRY_TYPE(Component);
    TRY_TYPE(Cod);
    TRY_TYPE(Qcd);
    TRY_TYPE(SotSegment);
    TRY_TYPE(Iplt);
    return 0;
}
