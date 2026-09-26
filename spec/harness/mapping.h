/* mapping.h — ACN mapping functions for box types and length determinants.
 *
 * The length mappings convert between model payload counts and wire
 * lengths in both directions. The box-type mappings are decode-only and
 * route unlisted TBox values to the opaque `other` choice. The generated
 * code dictates the prototype names and declares every mapping function
 * with asn1SccUint; check it when upgrading asn1scc.
 *
 *   lxxx : Lxxx counts its own two bytes         wire = n + 2
 *   psot : Psot counts from the SOT marker code  wire = n + 12
 *   lbox : LBox counts LBox and TBox             wire = n + 8
 *   boxtype : unknown wire type to opaque `other` on decode only
 *             (TopPayload, InnerPayload and their -Profile versions)
 *   resboxtype : as boxtype, inside a Resolution box: resc and resd
 *             (ResPayload)
 *   jp2boxtype : as boxtype, for a .jp2 at the profile layer, where every
 *             type but jp2c, jP and ftyp is `other` (Jp2Payload-Profile)
 *
 * ../check-model.sh checks each box-type list against the CHOICEs it
 * serves, and MAPPING_OTHER_BOX against their `other` alternatives.
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

/* The TBox value of every `other` alternative: 'abcd', a type no mapping
 * keeps. The encoder writes it for an `other` box. */
#define MAPPING_OTHER_BOX 1633837924u

asn1SccUint MAPPING_ENCODE_NAME(lxxx)(asn1SccUint n);
asn1SccUint MAPPING_DECODE_NAME(lxxx)(asn1SccUint n);
asn1SccUint MAPPING_ENCODE_NAME(psot)(asn1SccUint n);
asn1SccUint MAPPING_DECODE_NAME(psot)(asn1SccUint n);
asn1SccUint MAPPING_ENCODE_NAME(lbox)(asn1SccUint n);
asn1SccUint MAPPING_DECODE_NAME(lbox)(asn1SccUint n);
asn1SccUint MAPPING_DECODE_NAME(boxtype)(asn1SccUint type);
asn1SccUint MAPPING_DECODE_NAME(resboxtype)(asn1SccUint type);
asn1SccUint MAPPING_DECODE_NAME(jp2boxtype)(asn1SccUint type);

#endif
