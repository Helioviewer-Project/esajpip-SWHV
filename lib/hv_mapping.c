/* hv_mapping.c: the ACN mapping function lxxx, which the generated
 * *Segment encoders and decoders call, here and in the corpus harness
 * (../spec/harness/mapping.h declares it with the other mappings). Lxxx
 * counts its own two bytes (T.800 A.1). For a wire value below 2, return
 * the largest asn1SccUint as an invalid payload size. The generated
 * CONTAINING decoder rejects it at its bounds check before reading the
 * body. The mapping API itself cannot report failure. */
#include "asn1crt.h"

asn1SccUint lxxx_encode(asn1SccUint n);
asn1SccUint lxxx_decode(asn1SccUint n);

asn1SccUint lxxx_encode(asn1SccUint n) { return n + 2; }
asn1SccUint lxxx_decode(asn1SccUint n) { return n < 2 ? (asn1SccUint)-1 : n - 2; }
