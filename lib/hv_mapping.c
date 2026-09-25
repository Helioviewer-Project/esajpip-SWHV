/* hv_mapping.c: the ACN mapping function the generated *Segment-Std
 * decoders call. Lxxx counts its own two bytes (T.800 A.1.4). A wire value
 * below 2 wraps to a huge size, which the decoder rejects. */
#include "asn1crt.h"

asn1SccUint lxxx_encode(asn1SccUint n);
asn1SccUint lxxx_decode(asn1SccUint n);

asn1SccUint lxxx_encode(asn1SccUint n) { return n + 2; }
asn1SccUint lxxx_decode(asn1SccUint n) { return n - 2; }
