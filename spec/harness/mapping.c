/* mapping.c — see mapping.h. The length decode mappings are not
 * clamped. mapping.h declares them with asn1SccSint, but the generated code
 * declares and calls every mapping function with asn1SccUint (the same
 * width), so a wire value below the offset (e.g. Lcod = 1) reaches the
 * generated decoder as a huge payload length, which it then rejects. That
 * is the behaviour the corpus wants for invalid-length vectors. */
#include "mapping.h"

asn1SccSint MAPPING_ENCODE_NAME(lxxx)(asn1SccSint n) { return n + 2; }
asn1SccSint MAPPING_DECODE_NAME(lxxx)(asn1SccSint n) { return n - 2; }

asn1SccSint MAPPING_ENCODE_NAME(psot)(asn1SccSint n) { return n + 12; }
asn1SccSint MAPPING_DECODE_NAME(psot)(asn1SccSint n) { return n - 12; }

asn1SccSint MAPPING_ENCODE_NAME(lbox)(asn1SccSint n) { return n + 8; }
asn1SccSint MAPPING_DECODE_NAME(lbox)(asn1SccSint n) { return n - 8; }

/* Normalize types unknown to both box grammars for the opaque `other` branch.
 * Preserve all recognized types, at both levels: a type the CHOICE at its
 * level has no alternative for (a jpch or an asoc inside a superbox, an
 * ihdr at the top level) then fails to decode, which is how the model
 * rejects its placement: the corpus reason is `decode`, and
 * box.nested-superbox (hv_rule_child) never fires in the harness. The
 * original wire type is irrelevant to this validity-only model. */
asn1SccUint MAPPING_DECODE_NAME(boxtype)(asn1SccUint type) {
    switch (type) {
        case 1634955107u: /* asoc */
        case 1651532643u: /* bpcc */
        case 1667523942u: /* cdef */
        case 1668112752u: /* cmap */
        case 1668246642u: /* colr */
        case 1685348972u: /* dtbl */
        case 1718383476u: /* flst */
        case 1718773093u: /* free */
        case 1718903404u: /* ftbl */
        case 1718909296u: /* ftyp */
        case 1768449138u: /* ihdr */
        case 1783636000u: /* jP   */
        case 1785737827u: /* jp2c */
        case 1785737832u: /* jp2h */
        case 1785750376u: /* jpch */
        case 1785752680u: /* jplh */
        case 1818389536u: /* lbl  */
        case 1852601204u: /* nlst */
        case 1885564018u: /* pclr */
        case 1919251232u: /* res  */
        case 1920099697u: /* rreq */
        case 1969843814u: /* uinf */
        case 1970433056u: /* url  */
        case 1970628964u: /* uuid */
        case 2020437024u: /* xml  */
            return type;
        default:
            return 1633837924u; /* abcd: `other` choice determinant */
    }
}

/* Inside a Resolution box: its two child types, every other type `other`. */
asn1SccUint MAPPING_DECODE_NAME(resboxtype)(asn1SccUint type) {
    switch (type) {
        case 1919251299u: /* resc */
        case 1919251300u: /* resd */
            return type;
        default:
            return 1633837924u; /* abcd: `other` choice determinant */
    }
}

/* A .jp2 at the profile layer: ReadJP2 reads only these; every other box,
 * superboxes included, is opaque `other`. */
asn1SccUint MAPPING_DECODE_NAME(jp2boxtype)(asn1SccUint type) {
    switch (type) {
        case 1718909296u: /* ftyp */
        case 1783636000u: /* jP   */
        case 1785737827u: /* jp2c */
            return type;
        default:
            return 1633837924u; /* abcd: `other` choice determinant */
    }
}
