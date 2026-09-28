/* hv_codes.h: the JPEG 2000 marker codes (T.800 Table A.2) and box types
 * (T.800 Table I.2, T.801 Table M.8; the big-endian value of the
 * four-character code) the code refers to, in one place. The model states
 * most of them, as `present-when` values, fixed INTEGER fields and
 * termination patterns in ../spec; ../spec/check-model.sh checks that the
 * two agree, and lists those it does not state (SOP, EPH, 0xFF30 to
 * 0xFF3F, the box types the box tree rules alone read, and the brands). */
#ifndef HV_CODES_H
#define HV_CODES_H

enum {
    HV_SOC = 0xFF4F, HV_SIZ = 0xFF51, HV_COD = 0xFF52, HV_COC = 0xFF53, HV_TLM = 0xFF55,
    HV_PLM = 0xFF57, HV_PLT = 0xFF58, HV_QCD = 0xFF5C, HV_QCC = 0xFF5D, HV_RGN = 0xFF5E,
    HV_POC = 0xFF5F, HV_PPM = 0xFF60, HV_PPT = 0xFF61, HV_CRG = 0xFF63, HV_COM = 0xFF64,
    HV_SOT = 0xFF90, HV_SOP = 0xFF91, HV_EPH = 0xFF92, HV_SOD = 0xFF93, HV_EOC = 0xFFD9,
    /* A.1.3: codes without a marker segment, reserved for this purpose. */
    HV_NO_SEGMENT_FIRST = 0xFF30, HV_NO_SEGMENT_LAST = 0xFF3F
};

enum {
    /* T.800 Annex I */
    HV_BOX_JP = 0x6A502020, HV_BOX_FTYP = 0x66747970, HV_BOX_JP2H = 0x6A703268,
    HV_BOX_IHDR = 0x69686472, HV_BOX_BPCC = 0x62706363, HV_BOX_COLR = 0x636F6C72,
    HV_BOX_PCLR = 0x70636C72, HV_BOX_CMAP = 0x636D6170, HV_BOX_CDEF = 0x63646566,
    HV_BOX_RES = 0x72657320, HV_BOX_RESC = 0x72657363, HV_BOX_RESD = 0x72657364,
    HV_BOX_JP2C = 0x6A703263, HV_BOX_XML = 0x786D6C20, HV_BOX_UINF = 0x75696E66,
    HV_BOX_JP2I = 0x6A703269, HV_BOX_UUID = 0x75756964, HV_BOX_ULST = 0x756C7374,
    /* T.801 Annex M */
    HV_BOX_RREQ = 0x72726571, HV_BOX_JPCH = 0x6A706368, HV_BOX_JPLH = 0x6A706C68,
    HV_BOX_CGRP = 0x63677270, HV_BOX_FTBL = 0x6674626C, HV_BOX_FLST = 0x666C7374,
    HV_BOX_DTBL = 0x6474626C, HV_BOX_URL = 0x75726C20, HV_BOX_ASOC = 0x61736F63,
    HV_BOX_NLST = 0x6E6C7374, HV_BOX_COMP = 0x636F6D70, HV_BOX_DREP = 0x64726570,
    HV_BOX_J2CX = 0x6A326378, HV_BOX_CREG = 0x63726567, HV_BOX_LBL = 0x6C626C20,
    HV_BOX_MDAT = 0x6D646174, HV_BOX_CREF = 0x63726566, HV_BOX_OPCT = 0x6F706374,
    HV_BOX_PXFM = 0x7078666D, HV_BOX_COPT = 0x636F7074, HV_BOX_GTSO = 0x6774736F,
    HV_BOX_JCLX = 0x6A636C78, HV_BOX_GRP = 0x67727020, HV_BOX_J2CI = 0x6A326369,
    HV_BOX_JLXI = 0x6A6C7869, HV_BOX_INST = 0x696E7374, HV_BOX_FREE = 0x66726565
};

/* File type brands (T.800 I.5.2, T.801 M.8, M.9.2); jpxb is baseline JPX. */
enum { HV_BRAND_JP2 = 0x6A703220, HV_BRAND_JPX = 0x6A707820, HV_BRAND_JPXB = 0x6A707862 };

/* The contents of the JPEG 2000 Signature box (T.800 I.5.1), as an
 * initializer. */
#define HV_SIGNATURE_BYTES {0x0D, 0x0A, 0x87, 0x0A}

/* A box header (I.4): LBox and TBox, and XLBox when LBox = 1. */
enum { HV_BOX_HEADER = 8, HV_BOX_HEADER_XL = 16 };

/* The size of the largest encoding of a type generated from the model
 * (asn1scc's REQUIRED_BYTES): the most bytes its decoder reads and its
 * encoder writes. */
#define HV_LARGEST(T) ((size_t)T##_REQUIRED_BYTES_FOR_ACN_ENCODING)

/* The size of the encoding of a fixed-size type, as the standards lay it
 * out: HV_FIXED(T) is HV_<T>_BYTES below, so a type without an entry does
 * not compile, and hv_reader.c checks each entry against the model's
 * largest encoding. For a variable-size type (BoxHeader, Ihdr, ...) use
 * HV_LARGEST, the sizes above, or the length the decoder measured. */
enum {
    HV_MarkerCode_BYTES = 2,        /* T.800 A.1.1 */
    HV_SegmentLength_BYTES = 2,     /* Lxxx */
    HV_SotSegment_BYTES = 10,       /* SOT: Lsot, Isot, Psot, TPsot, TNsot */
    HV_Component_BYTES = 3,         /* SIZ: Ssiz, XRsiz, YRsiz */
    HV_Brand_BYTES = 4,             /* ftyp: BR, or a CLi */
    HV_FtypHeader_BYTES = 8,        /* ftyp: BR, MinV */
    HV_BitDepth_BYTES = 1,          /* bpcc: BPCi */
    HV_PclrCounts_BYTES = 3,        /* pclr: NE, NPC */
    HV_CmapEntry_BYTES = 4,         /* cmap: CMPi, MTYPi, PCOLi */
    HV_CdefEntry_BYTES = 6,         /* cdef: Cni, Typi, Asoci */
    HV_UrlHeader_BYTES = 4,         /* url: VERS, FLAG */
    HV_DataReferenceCount_BYTES = 2, /* dtbl: NDR */
    HV_Fragment_BYTES = 14,         /* flst: OFF, LEN, DR */
    HV_FeatureCode_BYTES = 2,       /* rreq: SFi */
    HV_NlstEntry_BYTES = 4,         /* nlst: ANi */
    HV_VendorId_BYTES = 16,         /* rreq: VFi, a UUID */
    HV_UuidCount_BYTES = 2,         /* ulst: NU */
    HV_UuidId_BYTES = 16,           /* ulst: IDi; uuid: ID */
    HV_CrefType_BYTES = 4           /* cref: Rtyp */
};
#define HV_FIXED(T) ((size_t)HV_##T##_BYTES)

#endif
