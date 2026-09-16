/* mapping.c — see mapping.h. The decode direction is deliberately not
 * clamped: a wire value below the offset (e.g. Lcod = 1) maps to a negative
 * payload length, which the generated decoder then rejects as an invalid
 * size determinant. That is the behaviour the corpus wants for
 * invalid-length vectors. */
#include "mapping.h"

asn1SccSint MAPPING_ENCODE_NAME(lxxx)(asn1SccSint n) { return n + 2; }
asn1SccSint MAPPING_DECODE_NAME(lxxx)(asn1SccSint n) { return n - 2; }

asn1SccSint MAPPING_ENCODE_NAME(psot)(asn1SccSint n) { return n + 12; }
asn1SccSint MAPPING_DECODE_NAME(psot)(asn1SccSint n) { return n - 12; }

asn1SccSint MAPPING_ENCODE_NAME(lbox)(asn1SccSint n) { return n + 8; }
asn1SccSint MAPPING_DECODE_NAME(lbox)(asn1SccSint n) { return n - 8; }
