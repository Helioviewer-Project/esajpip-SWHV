/* hv_mapping.c: the ACN mapping function lxxx, which the generated
 * *Segment encoders and decoders call, here and in the corpus harness
 * (../spec/harness/mapping.h declares it with the other mappings). Lxxx
 * counts its own two bytes (T.800 A.1.4). A wire value below 2 wraps to a
 * huge size, which the decoder rejects. */
#include "asn1crt.h"

asn1SccUint lxxx_encode(asn1SccUint n);
asn1SccUint lxxx_decode(asn1SccUint n);

asn1SccUint lxxx_encode(asn1SccUint n) { return n + 2; }
asn1SccUint lxxx_decode(asn1SccUint n) { return n - 2; }
