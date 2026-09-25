#ifndef ASN1SCC_ASN1CRT_ENCODING_ACN_H_
#define ASN1SCC_ASN1CRT_ENCODING_ACN_H_

#include "asn1crt_encoding_uper.h"

#ifdef  __cplusplus
extern "C" {
#endif


/*

       db         ,ad8888ba,   888b      88           88888888888                                             88
      d88b       d8"'    `"8b  8888b     88           88                                               ,d     ""
     d8'`8b     d8'            88 `8b    88           88                                               88
    d8'  `8b    88             88  `8b   88           88aaaaa  88       88  8b,dPPYba,    ,adPPYba,  MM88MMM  88   ,adPPYba,   8b,dPPYba,   ,adPPYba,
   d8YaaaaY8b   88             88   `8b  88           88"""""  88       88  88P'   `"8a  a8"     ""    88     88  a8"     "8a  88P'   `"8a  I8[    ""
  d8""""""""8b  Y8,            88    `8b 88           88       88       88  88       88  8b            88     88  8b       d8  88       88   `"Y8ba,
 d8'        `8b  Y8a.    .a8P  88     `8888           88       "8a,   ,a88  88       88  "8a,   ,aa    88,    88  "8a,   ,a8"  88       88  aa    ]8I
d8'          `8b  `"Y8888Y"'   88      `888           88        `"YbbdP'Y8  88       88   `"Ybbd8"'    "Y888  88   `"YbbdP"'   88       88  `"YbbdP"
*/


/*ACN Integer functions*/
void Acn_Enc_Int_PositiveInteger_ConstSize(BitStream* pBitStrm, asn1SccUint intVal, int encodedSizeInBits);
void Acn_Enc_Int_PositiveInteger_ConstSize_8(BitStream* pBitStrm, asn1SccUint intVal);
void Acn_Enc_Int_PositiveInteger_ConstSize_big_endian_16(BitStream* pBitStrm, asn1SccUint intVal);
void Acn_Enc_Int_PositiveInteger_ConstSize_big_endian_32(BitStream* pBitStrm, asn1SccUint intVal);
void Acn_Enc_Int_PositiveInteger_ConstSize_big_endian_64(BitStream* pBitStrm, asn1SccUint intVal);






/*ACN Decode Integer functions*/
flag Acn_Dec_Int_PositiveInteger_ConstSize(BitStream* pBitStrm, asn1SccUint* pIntVal, int encodedSizeInBits);
flag Acn_Dec_Int_PositiveInteger_ConstSize_8(BitStream* pBitStrm, asn1SccUint* pIntVal);
flag Acn_Dec_Int_PositiveInteger_ConstSize_big_endian_16(BitStream* pBitStrm, asn1SccUint* pIntVal);
flag Acn_Dec_Int_PositiveInteger_ConstSize_big_endian_32(BitStream* pBitStrm, asn1SccUint* pIntVal);
flag Acn_Dec_Int_PositiveInteger_ConstSize_big_endian_64(BitStream* pBitStrm, asn1SccUint* pIntVal);


/*encoding ends when 'F' is reached*/



/*flag Acn_Dec_Int_ASCII_NullTerminated_FormattedInteger(BitStream* pBitStrm, const char* format, asn1SccSint* pIntVal);*/











































































/* Boolean Decode */

flag BitStream_ReadBitPattern(BitStream* pBitStrm, const byte* patternToRead, int nBitsToRead, flag* pBoolValue);

/* NULL Type functions*/

/*Real encoding functions*/



/*Real encoded as scaled integer (linear mapping between the REAL range and the integer range)*/

/*String functions*/



/* Length Determinant functions*/





/*
   ACN Deferred Patching — types and helpers for reserve-space + patch-later pattern.
   Used when --acn-deferred flag is enabled.
*/

/* Position in a bitstream — used to remember where to patch later */
typedef struct {
    long currentByte;
    int  currentBit;   /* 0..7 */
} AcnBitStreamPos;

/* Reference to an ACN-inserted field that will be patched later.
 * is_set + value enable consistency checking for shared determinants. */
typedef struct {
    AcnBitStreamPos pos;      /* where in the bitstream the field was reserved */
    flag            is_set;   /* TRUE after first PatchDet call */
    asn1SccUint     value;    /* the value that was written (for consistency checking) */
    char            str_value[256]; /* for IA5String determinants (consistency checking) */
} AcnInsertedFieldRef;

static inline AcnBitStreamPos Acn_BitStream_GetPos(const BitStream* bs) {
    AcnBitStreamPos p;
    p.currentByte = bs->currentByte;
    p.currentBit = bs->currentBit;
    return p;
}

static inline void Acn_BitStream_SetPos(BitStream* bs, AcnBitStreamPos p) {
    bs->currentByte = p.currentByte;
    bs->currentBit = p.currentBit;
}

static inline asn1SccUint Acn_BitStream_DistanceInBytes(AcnBitStreamPos start, AcnBitStreamPos end) {
    long startBits = start.currentByte * 8 + start.currentBit;
    long endBits = end.currentByte * 8 + end.currentBit;
    return (asn1SccUint)((endBits - startBits + 7) / 8);
}

static inline asn1SccUint Acn_BitStream_DistanceInBits(AcnBitStreamPos start, AcnBitStreamPos end) {
    long startBits = start.currentByte * 8 + start.currentBit;
    long endBits = end.currentByte * 8 + end.currentBit;
    return (asn1SccUint)(endBits - startBits);
}

/*
 * DEFINE_ACN_DET_ENCODERS(name, encoder_fn, size_bits)
 *
 * Generates InitDet/PatchDet wrappers for a specific integer encoding class.
 * - InitDet: saves current bitstream position, writes placeholder zeros
 * - PatchDet: seeks back to saved position, writes the actual value, restores position.
 *   For shared determinants (same field referenced by multiple children),
 *   the first call writes the value; subsequent calls verify consistency.
 */
#define DEFINE_ACN_DET_ENCODERS(name, encoder_fn, size_bits)                    \
static inline void Acn_InitDet_##name(BitStream* bs,                            \
                                      AcnInsertedFieldRef* det) {               \
    det->pos = Acn_BitStream_GetPos(bs);                                        \
    det->is_set = FALSE;                                                        \
    det->value = 0;                                                             \
    encoder_fn(bs, 0);                                                          \
}                                                                               \
static inline flag Acn_PatchDet_##name(asn1SccUint v, BitStream* bs,            \
                                       AcnInsertedFieldRef* det, int* err) {    \
    if (!det->is_set) {                                                         \
        AcnBitStreamPos cur = Acn_BitStream_GetPos(bs);                         \
        Acn_BitStream_SetPos(bs, det->pos);                                     \
        encoder_fn(bs, v);                                                      \
        Acn_BitStream_SetPos(bs, cur);                                          \
        det->value = v;                                                         \
        det->is_set = TRUE;                                                     \
        return TRUE;                                                            \
    } else {                                                                    \
        if (det->value != v) {                                                  \
            if (err) *err = ERR_ACN_DET_CONSISTENCY_MISMATCH;                   \
            return FALSE;                                                       \
        }                                                                       \
        return TRUE;                                                            \
    }                                                                           \
}

/* Unsigned integer encoding classes */
DEFINE_ACN_DET_ENCODERS(U8,      Acn_Enc_Int_PositiveInteger_ConstSize_8, 8)
DEFINE_ACN_DET_ENCODERS(U16_BE,  Acn_Enc_Int_PositiveInteger_ConstSize_big_endian_16, 16)
DEFINE_ACN_DET_ENCODERS(U32_BE,  Acn_Enc_Int_PositiveInteger_ConstSize_big_endian_32, 32)
DEFINE_ACN_DET_ENCODERS(U64_BE,  Acn_Enc_Int_PositiveInteger_ConstSize_big_endian_64, 64)

/* Variant for signed (two's complement) encoder functions that take asn1SccSint */
#define DEFINE_ACN_DET_ENCODERS_SIGNED(name, encoder_fn, size_bits)             \
static inline void Acn_InitDet_##name(BitStream* bs,                            \
                                      AcnInsertedFieldRef* det) {               \
    det->pos = Acn_BitStream_GetPos(bs);                                        \
    det->is_set = FALSE;                                                        \
    det->value = 0;                                                             \
    encoder_fn(bs, 0);                                                          \
}                                                                               \
static inline flag Acn_PatchDet_##name(asn1SccUint v, BitStream* bs,            \
                                       AcnInsertedFieldRef* det, int* err) {    \
    if (!det->is_set) {                                                         \
        AcnBitStreamPos cur = Acn_BitStream_GetPos(bs);                         \
        Acn_BitStream_SetPos(bs, det->pos);                                     \
        encoder_fn(bs, (asn1SccSint)v);                                         \
        Acn_BitStream_SetPos(bs, cur);                                          \
        det->value = v;                                                         \
        det->is_set = TRUE;                                                     \
        return TRUE;                                                            \
    } else {                                                                    \
        if (det->value != v) {                                                  \
            if (err) *err = ERR_ACN_DET_CONSISTENCY_MISMATCH;                   \
            return FALSE;                                                       \
        }                                                                       \
        return TRUE;                                                            \
    }                                                                           \
}

/* Signed (two's complement) integer encoding classes */

/* Boolean determinant: 1-bit default encoding (writes 0 or 1 as a single bit) */
static inline void Acn_Enc_Bool_1bit(BitStream* bs, asn1SccUint v) {
    (void)bs; (void)v;
    BitStream_AppendBit(bs, v ? 1 : 0);
}
DEFINE_ACN_DET_ENCODERS(BOOL1, Acn_Enc_Bool_1bit, 1)

/* Generic ConstSize — arbitrary bit width.
 * Like DEFINE_ACN_DET_ENCODERS but the encoder takes 3 args (bs, value, nBits)
 * instead of 2.  The extra nBits parameter is forwarded by InitDet/PatchDet.
 * Uses macro approach so that function names (Acn_InitDet_##name) are created
 * via token pasting — this keeps them invisible to the line-based RTL header
 * pruning, avoiding collateral damage from removeFunctionFromHeader. */
#define DEFINE_ACN_DET_ENCODERS_CONSTSIZE(name, encoder_fn)                     \
static inline void Acn_InitDet_##name(BitStream* bs, int nBits,                 \
                                      AcnInsertedFieldRef* det) {               \
    (void)nBits;                                                                \
    det->pos = Acn_BitStream_GetPos(bs);                                        \
    det->is_set = FALSE;                                                        \
    det->value = 0;                                                             \
    encoder_fn(bs, 0, nBits);                                                   \
}                                                                               \
static inline flag Acn_PatchDet_##name(asn1SccUint v, BitStream* bs, int nBits, \
                                       AcnInsertedFieldRef* det, int* err) {    \
    (void)nBits;                                                                \
    if (!det->is_set) {                                                         \
        int _i;                                                                 \
        AcnBitStreamPos cur = Acn_BitStream_GetPos(bs);                         \
        Acn_BitStream_SetPos(bs, det->pos);                                     \
        /* Bit-by-bit overwrite: encoder_fn uses AppendPartialByte which   */ \
        /* clears adjacent bits when crossing a byte boundary — unsafe   */ \
        /* for patching in the middle of a stream. AppendBit is safe.    */ \
        for (_i = 0; _i < nBits; _i++)                                          \
            BitStream_AppendBit(bs, (byte)((v >> (nBits - 1 - _i)) & 1));       \
        Acn_BitStream_SetPos(bs, cur);                                          \
        det->value = v;                                                         \
        det->is_set = TRUE;                                                     \
        return TRUE;                                                            \
    } else {                                                                    \
        if (det->value != v) {                                                  \
            if (err) *err = ERR_ACN_DET_CONSISTENCY_MISMATCH;                   \
            return FALSE;                                                       \
        }                                                                       \
        return TRUE;                                                            \
    }                                                                           \
}

#define DEFINE_ACN_DET_ENCODERS_SIGNED_CONSTSIZE(name, encoder_fn)              \
static inline void Acn_InitDet_##name(BitStream* bs, int nBits,                 \
                                      AcnInsertedFieldRef* det) {               \
    (void)nBits;                                                                \
    det->pos = Acn_BitStream_GetPos(bs);                                        \
    det->is_set = FALSE;                                                        \
    det->value = 0;                                                             \
    encoder_fn(bs, 0, nBits);                                                   \
}                                                                               \
static inline flag Acn_PatchDet_##name(asn1SccUint v, BitStream* bs, int nBits, \
                                       AcnInsertedFieldRef* det, int* err) {    \
    (void)nBits;                                                                \
    if (!det->is_set) {                                                         \
        int _i;                                                                 \
        AcnBitStreamPos cur = Acn_BitStream_GetPos(bs);                         \
        Acn_BitStream_SetPos(bs, det->pos);                                     \
        /* Bit-by-bit write (same rationale as unsigned CONSTSIZE variant) */    \
        for (_i = 0; _i < nBits; _i++)                                          \
            BitStream_AppendBit(bs, (byte)((v >> (nBits - 1 - _i)) & 1));       \
        Acn_BitStream_SetPos(bs, cur);                                          \
        det->value = v;                                                         \
        det->is_set = TRUE;                                                     \
        return TRUE;                                                            \
    } else {                                                                    \
        if (det->value != v) {                                                  \
            if (err) *err = ERR_ACN_DET_CONSISTENCY_MISMATCH;                   \
            return FALSE;                                                       \
        }                                                                       \
        return TRUE;                                                            \
    }                                                                           \
}

DEFINE_ACN_DET_ENCODERS_CONSTSIZE(ConstSize, Acn_Enc_Int_PositiveInteger_ConstSize)

/* IA5String determinant: fixed-size ASCII string (7 bits per character).
 * Defined in asn1crt_encoding_acn.c (not inline) to avoid header pruning
 * collateral damage — the body calls BitStream_AppendBit which, if pruned
 * from the header, would break macro bodies that reference it. */


#ifdef  __cplusplus
}
#endif

#endif
