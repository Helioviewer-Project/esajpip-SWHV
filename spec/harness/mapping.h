/* mapping.h — ACN mapping functions for the inserted length determinants.
 *
 * asn1scc calls a mapping function on a determinant's value: the encode
 * direction maps the model value (payload byte count) to the wire value,
 * the decode direction maps the wire value back. The exact prototype names
 * are dictated by the generated header for the module that references
 * them; check it and adjust MAPPING_ENCODE_NAME / MAPPING_DECODE_NAME.
 *
 *   lxxx : Lxxx counts its own two bytes         wire = n + 2
 *   psot : Psot counts from the SOT marker code  wire = n + 12
 *   lbox : LBox counts LBox and TBox             wire = n + 8
 */
#ifndef J2K_HARNESS_MAPPING_H
#define J2K_HARNESS_MAPPING_H

#include "asn1crt.h"

#ifndef MAPPING_ENCODE_NAME
#define MAPPING_ENCODE_NAME(f) f##_encode
#endif
#ifndef MAPPING_DECODE_NAME
#define MAPPING_DECODE_NAME(f) f##_decode
#endif

asn1SccSint MAPPING_ENCODE_NAME(lxxx)(asn1SccSint n);
asn1SccSint MAPPING_DECODE_NAME(lxxx)(asn1SccSint n);
asn1SccSint MAPPING_ENCODE_NAME(psot)(asn1SccSint n);
asn1SccSint MAPPING_DECODE_NAME(psot)(asn1SccSint n);
asn1SccSint MAPPING_ENCODE_NAME(lbox)(asn1SccSint n);
asn1SccSint MAPPING_DECODE_NAME(lbox)(asn1SccSint n);

#endif
