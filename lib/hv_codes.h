/* hv_codes.h: the JPEG 2000 marker codes (T.800 Table A.2) and box types
 * (T.800 Table I.2, T.801 Table M.8; the big-endian value of the
 * four-character code) the code refers to, in one place. The model states
 * them as the `present-when` values and ranges of the ACN files in ../spec. */
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
    /* T.801 Annex M */
    HV_BOX_RREQ = 0x72726571, HV_BOX_JPCH = 0x6A706368, HV_BOX_JPLH = 0x6A706C68,
    HV_BOX_CGRP = 0x63677270, HV_BOX_FTBL = 0x6674626C, HV_BOX_FLST = 0x666C7374,
    HV_BOX_DTBL = 0x6474626C, HV_BOX_URL = 0x75726C20, HV_BOX_ASOC = 0x61736F63,
    HV_BOX_NLST = 0x6E6C7374, HV_BOX_COMP = 0x636F6D70, HV_BOX_DREP = 0x64726570,
    HV_BOX_J2CX = 0x6A326378
};

/* File type brands (T.800 I.5.2, T.801 M.11.1.2). */
enum { HV_BRAND_JP2 = 0x6A703220, HV_BRAND_JPX = 0x6A707820 };

/* A box header (I.4): LBox and TBox, and XLBox when LBox = 1. */
enum { HV_BOX_HEADER = 8, HV_BOX_HEADER_XL = 16 };

/* The size of the encoding of a fixed-size type generated from the model:
 * its largest (asn1scc's REQUIRED_BYTES), which is its only one. */
#define HV_FIXED(T) ((size_t)T##_REQUIRED_BYTES_FOR_ACN_ENCODING)

#endif
