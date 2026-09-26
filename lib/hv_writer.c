/* hv_writer.c: see hv_writer.h. */
#include "hv_writer.h"

#include "hv_codes.h"
#include "jp2-boxes.h"        /* Fragment, the Reader Requirements features */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* The largest box LBox can state. test/test_writer.c includes this file
 * with a lower value to exercise the switch to XLBox. */
#ifndef HV_LBOX_MAX
#define HV_LBOX_MAX UINT32_MAX
#endif

/* Records the first error; every later write then does nothing. */
static int out_fail(hv_out *out, const char *error) {
    if (out->error == NULL)
        out->error = error;
    return -1;
}

void hv_out_init(hv_out *out) {
    memset(out, 0, sizeof *out);
}

void hv_out_free(hv_out *out) {
    free(out->data);
    memset(out, 0, sizeof *out);
}

/* The bytes from out->size to out->capacity are always zero: reserve zeroes
 * what it adds, hv_out_rewind what it drops, and a failed encoder what it
 * wrote there (ENCODE). The generated encoders rely on it. */
void hv_out_rewind(hv_out *out, size_t size) {
    if (size < out->size) {
        memset(out->data + size, 0, out->size - size);
        out->size = size;
    }
    out->error = NULL;
}

/* Room for `more` bytes after out->size. */
static int reserve(hv_out *out, size_t more) {
    size_t need, capacity;
    uint8_t *data;

    if (out->error != NULL)
        return -1;
    if (more > SIZE_MAX - out->size)
        return out_fail(out, "output larger than SIZE_MAX bytes");
    need = out->size + more;
    if (need <= out->capacity)
        return 0;
    capacity = out->capacity ? out->capacity : 4096;
    while (capacity < need)
        capacity = capacity > SIZE_MAX / 2 ? need : capacity * 2;
    if ((data = realloc(out->data, capacity)) == NULL)
        return out_fail(out, "out of memory");
    memset(data + out->capacity, 0, capacity - out->capacity);
    out->data = data;
    out->capacity = capacity;
    return 0;
}

/* Runs a generated encoder at `at`, with room for its largest encoding:
 * the generated encoders do not check the room themselves. Every type
 * here is small, a fixed part or one element of a list (at most 197 bytes,
 * QcdSegment), so the room costs nothing. They also skip zero bits
 * instead of writing them (BitStream_AppendNBitZero only moves on), so the
 * bytes they write must be zero first. Past out->size they are (see
 * hv_out_rewind), so an append (`at` = out->size) clears nothing; an
 * in-place patch clears exactly the `clear` bytes of the header it
 * replaces, which lie below out->size. What an encoder that fails wrote
 * past out->size is zeroed again. `what` names the type in the error
 * message. */
#define ENCODE(T, value, out, at, clear, written)                            \
    encode_##T((value), (out), (at), (clear), (written))

#define DEFINE_ENCODE(T, what)                                               \
    static int encode_##T(const T *value, hv_out *out, size_t at,           \
                          size_t clear, size_t *written) {                  \
        BitStream s;                                                         \
        int err = 0;                                                         \
        size_t room = HV_LARGEST(T);                                         \
        if (out->error != NULL)                                              \
            return -1;                                                       \
        if (at > out->size || clear > out->size - at)                        \
            return out_fail(out, "no " what " at the given offset");         \
        if (room > out->size - at && reserve(out, room - (out->size - at)) != 0) \
            return -1;                                                       \
        memset(out->data + at, 0, clear);                                    \
        BitStream_AttachBuffer(&s, out->data + at, (long)room);              \
        if (!T##_ACN_Encode(value, &s, &err, TRUE)) {                       \
            if (at + room > out->size)                                       \
                memset(out->data + out->size, 0, at + room - out->size);     \
            return out_fail(out, "invalid " what);                           \
        }                                                                    \
        *written = (size_t)BitStream_GetLength(&s);                          \
        return 0;                                                            \
    }

DEFINE_ENCODE(BoxHeader, "box header")
DEFINE_ENCODE(MarkerCode, "marker code")
DEFINE_ENCODE(SegmentLength, "marker segment length")
DEFINE_ENCODE(SotSegment, "SOT segment")
DEFINE_ENCODE(SizFixed, "SIZ segment")
DEFINE_ENCODE(Component, "SIZ segment")
DEFINE_ENCODE(CodSegment, "COD segment")
DEFINE_ENCODE(QcdSegment, "QCD segment")
DEFINE_ENCODE(Zplt, "PLT segment")
DEFINE_ENCODE(Iplt, "PLT segment")
DEFINE_ENCODE(Rcom, "COM segment")
DEFINE_ENCODE(FragmentCount, "Fragment List box contents")
DEFINE_ENCODE(Fragment, "Fragment List box contents")
DEFINE_ENCODE(RreqHeader, "Reader Requirements box contents")
DEFINE_ENCODE(RreqStandardFeature, "Reader Requirements box contents")
DEFINE_ENCODE(FeatureCount, "Reader Requirements box contents")
DEFINE_ENCODE(RreqVendorFeature, "Reader Requirements box contents")
DEFINE_ENCODE(DataReferenceCount, "NDR")
DEFINE_ENCODE(UrlHeader, "URL box VERS and FLAG")

/* Reads back a header this writer wrote, with the generated decoder, before
 * patching it in place. */
#define READ_BACK(T, value, out, at, size)                                   \
    read_back_##T((value), (out), (at), (size))

#define DEFINE_READ_BACK(T, what)                                            \
    static int read_back_##T(T *value, hv_out *out, size_t at, size_t size) { \
        BitStream s;                                                         \
        int err = 0;                                                         \
        if (at > out->size || size > out->size - at)                         \
            return out_fail(out, "no " what " at the given offset");         \
        BitStream_AttachBuffer(&s, out->data + at, (long)size);              \
        if (!T##_ACN_Decode(value, &s, &err))                               \
            return out_fail(out, "no " what " at the given offset");         \
        return 0;                                                            \
    }

DEFINE_READ_BACK(BoxHeader, "box header")
DEFINE_READ_BACK(SotSegment, "SOT segment")

/* Appends a generated encoding. */
#define APPEND(T, value, out)                                                \
    append_##T((value), (out))

#define DEFINE_APPEND(T)                                                     \
    static int append_##T(const T *value, hv_out *out) {                    \
        size_t written;                                                      \
        if (ENCODE(T, value, out, out->size, 0, &written) != 0)              \
            return -1;                                                       \
        out->size += written;                                                \
        return 0;                                                            \
    }

DEFINE_APPEND(BoxHeader)
DEFINE_APPEND(MarkerCode)
DEFINE_APPEND(SegmentLength)
DEFINE_APPEND(SotSegment)
DEFINE_APPEND(SizFixed)
DEFINE_APPEND(Component)
DEFINE_APPEND(CodSegment)
DEFINE_APPEND(QcdSegment)
DEFINE_APPEND(Zplt)
DEFINE_APPEND(Iplt)
DEFINE_APPEND(Rcom)
DEFINE_APPEND(FragmentCount)
DEFINE_APPEND(Fragment)
DEFINE_APPEND(DataReferenceCount)
DEFINE_APPEND(UrlHeader)
DEFINE_APPEND(RreqHeader)
DEFINE_APPEND(RreqStandardFeature)
DEFINE_APPEND(FeatureCount)
DEFINE_APPEND(RreqVendorFeature)

/* A marker code (A.1.1). */
#define MARKER HV_FIXED(MarkerCode)

int hv_write_bytes(hv_out *out, const void *bytes, size_t size) {
    if (reserve(out, size) != 0)
        return -1;
    if (size != 0)
        memcpy(out->data + out->size, bytes, size);
    out->size += size;
    return 0;
}

int hv_write_marker(hv_out *out, uint16_t code) {
    MarkerCode m = code;
    return APPEND(MarkerCode, &m, out);
}

/* A marker segment written piece by piece: the code and an Lxxx that
 * end_segment sets to the bytes written since, as the other lengths the
 * writer fills in (Psot, LBox). */
static int begin_segment(hv_out *out, uint16_t code, size_t *start) {
    SegmentLength l = HV_FIXED(SegmentLength);        /* a placeholder */
    *start = out->size;
    return hv_write_marker(out, code) != 0 ? -1 : APPEND(SegmentLength, &l, out);
}

static int end_segment(hv_out *out, size_t start) {
    SegmentLength l;
    size_t length, written;

    if (out->error != NULL)
        return -1;
    if (start > out->size || out->size - start < MARKER + HV_FIXED(SegmentLength))
        return out_fail(out, "marker segment shorter than its code and Lxxx");
    length = out->size - start - MARKER;                /* Lxxx counts itself */
    if (length > UINT16_MAX)
        return out_fail(out, "marker segment longer than 65,535 bytes (Lxxx)");
    l = length;
    return ENCODE(SegmentLength, &l, out, start + MARKER, HV_FIXED(SegmentLength), &written);
}

int hv_write_segment(hv_out *out, uint16_t code, const uint8_t *body, size_t size) {
    size_t start;
    if (out->error != NULL)
        return -1;
    if (size > UINT16_MAX - HV_FIXED(SegmentLength))      /* Lxxx counts itself */
        return out_fail(out, "marker segment body longer than 65,533 bytes");
    if (begin_segment(out, code, &start) != 0 || hv_write_bytes(out, body, size) != 0)
        return -1;
    return end_segment(out, start);
}

int hv_write_siz(hv_out *out, const hv_siz *siz) {
    size_t start, i;
    if (begin_segment(out, HV_SIZ, &start) != 0 || APPEND(SizFixed, siz->fixed, out) != 0)
        return -1;
    for (i = 0; i < siz->ncomponents; i++)
        if (APPEND(Component, &siz->components[i], out) != 0)
            return -1;
    return end_segment(out, start);
}

int hv_write_cod(hv_out *out, const CodSegment *cod) {
    return hv_write_marker(out, HV_COD) != 0 ? -1 : APPEND(CodSegment, cod, out);
}

int hv_write_qcd(hv_out *out, const QcdSegment *qcd) {
    return hv_write_marker(out, HV_QCD) != 0 ? -1 : APPEND(QcdSegment, qcd, out);
}

int hv_write_com(hv_out *out, Rcom rcom, const uint8_t *text, size_t size) {
    size_t start;
    if (out->error != NULL)
        return -1;
    if (size == 0)                                  /* Ccom: at least one byte */
        return out_fail(out, "COM segment without text");
    if (begin_segment(out, HV_COM, &start) != 0 || APPEND(Rcom, &rcom, out) != 0 ||
        hv_write_bytes(out, text, size) != 0)
        return -1;
    return end_segment(out, start);
}

/* One Iplt entry: 7-bit groups, most significant first. */
static void set_iplt(Iplt *p, uint64_t value) {
    IpltByte *more[9] = {&p->b0, &p->b1, &p->b2, &p->b3, &p->b4,
                         &p->b5, &p->b6, &p->b7, &p->b8};
    unsigned groups[10];                          /* b0 to b9 of the model's Iplt */
    int n = 0, i;

    do {
        groups[n++] = (unsigned)(value & 127);
        value >>= 7;
    } while (value != 0);
    memset(p, 0, sizeof *p);
    for (i = 0; i < n; i++) {
        unsigned bits = groups[n - 1 - i];
        if (i < 9)
            more[i]->bits = bits;
        else
            p->b9.bits = bits;
    }
    p->exist.b1 = n > 1;
    p->exist.b2 = n > 2;
    p->exist.b3 = n > 3;
    p->exist.b4 = n > 4;
    p->exist.b5 = n > 5;
    p->exist.b6 = n > 6;
    p->exist.b7 = n > 7;
    p->exist.b8 = n > 8;
    p->exist.b9 = n > 9;
}

int hv_write_plt(hv_out *out, const uint64_t *lengths, size_t count) {
    size_t i, start = 0, before;
    Zplt zplt = 0;
    int open = 0;

    for (i = 0; i < count; i++) {
        Iplt entry;
        if (out->error != NULL)
            return -1;
        if (lengths[i] == 0)
            return out_fail(out, "zero packet length");
        if (!open) {
            if (zplt > UINT8_MAX)                       /* Zplt, A.7.3 */
                return out_fail(out, "more than 256 PLT segments in a tile-part");
            if (begin_segment(out, HV_PLT, &start) != 0 || APPEND(Zplt, &zplt, out) != 0)
                return -1;
            zplt++;
            open = 1;
        }
        set_iplt(&entry, lengths[i]);
        before = out->size;
        if (APPEND(Iplt, &entry, out) != 0)
            return -1;
        /* Lplt, which counts itself, Zplt and the entries, is at most
         * 65,535: an entry that does not fit starts the next segment. */
        if (out->size - start - MARKER > UINT16_MAX) {
            hv_out_rewind(out, before);
            if (end_segment(out, start) != 0)
                return -1;
            open = 0;
            i--;
        }
    }
    return open ? end_segment(out, start) : out->error != NULL ? -1 : 0;
}

static SotSegment make_sot(uint16_t isot, uint32_t psot, uint8_t tpsot, uint8_t tnsot) {
    SotSegment sot;
    sot.lsot = HV_FIXED(SotSegment);            /* Lsot: the segment, marker code aside */
    sot.isot = isot;
    sot.psot = psot;
    sot.tpsot = tpsot;
    sot.tnsot = tnsot;
    return sot;
}

int hv_begin_tile_part(hv_out *out, uint16_t isot, uint8_t tpsot, uint8_t tnsot,
                       size_t *start) {
    SotSegment sot = make_sot(isot, 0, tpsot, tnsot);
    *start = out->size;
    if (hv_write_marker(out, HV_SOT) != 0)
        return -1;
    return APPEND(SotSegment, &sot, out);
}

int hv_end_tile_part(hv_out *out, size_t start) {
    size_t length, written;
    SotSegment sot;

    if (out->error != NULL)
        return -1;
    if (start > out->size || out->size - start < MARKER + HV_FIXED(SotSegment) + MARKER)
        return out_fail(out, "tile-part shorter than SOT and SOD");
    length = out->size - start;
    if (length > UINT32_MAX)
        return out_fail(out, "tile-part longer than 4,294,967,295 bytes (Psot)");
    if (READ_BACK(SotSegment, &sot, out, start + MARKER, HV_FIXED(SotSegment)) != 0)
        return -1;
    sot.psot = length;
    return ENCODE(SotSegment, &sot, out, start + MARKER, HV_FIXED(SotSegment), &written);
}

int hv_begin_box(hv_out *out, uint32_t type, int extended, size_t *start) {
    BoxHeader h;
    memset(&h, 0, sizeof h);
    h.lbox = extended ? 1 : HV_BOX_HEADER;           /* placeholders for hv_end_box */
    h.tbox = type;
    h.xlbox = HV_BOX_HEADER_XL;
    h.exist.xlbox = extended != 0;
    *start = out->size;
    return APPEND(BoxHeader, &h, out);
}

int hv_end_box(hv_out *out, size_t start) {
    BoxHeader h;
    size_t length, written;

    if (out->error != NULL)
        return -1;
    if (start > out->size || out->size - start < HV_BOX_HEADER)
        return out_fail(out, "box shorter than its header");
    length = out->size - start;
    if (READ_BACK(BoxHeader, &h, out, start, length < HV_BOX_HEADER_XL ? length : HV_BOX_HEADER_XL)
        != 0)
        return -1;
    if (h.exist.xlbox) {
        h.xlbox = length;
    } else if (length > HV_LBOX_MAX) {
        /* The box outgrew LBox: move its payload up to make room for XLBox
         * (I.4). */
        size_t more = HV_BOX_HEADER_XL - HV_BOX_HEADER;
        if (reserve(out, more) != 0)
            return -1;
        memmove(out->data + start + HV_BOX_HEADER_XL, out->data + start + HV_BOX_HEADER,
                length - HV_BOX_HEADER);
        out->size += more;
        h.lbox = 1;
        h.xlbox = length + more;
        h.exist.xlbox = TRUE;
    } else {
        h.lbox = length;
    }
    return ENCODE(BoxHeader, &h, out, start, h.exist.xlbox ? HV_BOX_HEADER_XL : HV_BOX_HEADER,
                  &written);
}

int hv_write_box_header(hv_out *out, uint32_t type, uint64_t size) {
    BoxHeader h;
    memset(&h, 0, sizeof h);
    h.tbox = type;
    if (size > HV_LBOX_MAX - HV_BOX_HEADER) {
        h.lbox = 1;
        h.xlbox = size + HV_BOX_HEADER_XL;
        h.exist.xlbox = TRUE;
    } else {
        h.lbox = size + HV_BOX_HEADER;
    }
    return APPEND(BoxHeader, &h, out);
}

int hv_write_flst(hv_out *out, uint64_t offset, uint32_t length, uint16_t dr) {
    FragmentCount nf = 1;
    Fragment f;
    size_t start;
    f.off = offset;
    f.len = length;
    f.dr = dr;
    return hv_begin_box(out, HV_BOX_FLST, 0, &start) != 0 || APPEND(FragmentCount, &nf, out) != 0 ||
           APPEND(Fragment, &f, out) != 0 ? -1 : hv_end_box(out, start);
}

int hv_write_ndr(hv_out *out, uint16_t ndr) {
    DataReferenceCount c = ndr;
    return APPEND(DataReferenceCount, &c, out);
}

int hv_write_url(hv_out *out, const char *loc) {
    UrlHeader h = {0, 0};
    size_t start;
    return hv_begin_box(out, HV_BOX_URL, 0, &start) != 0 || APPEND(UrlHeader, &h, out) != 0 ||
           hv_write_bytes(out, loc, strlen(loc) + 1) != 0 ? -1 : hv_end_box(out, start);
}

int hv_write_rreq(hv_out *out, const RreqHeader *header, const RreqStandardFeature *standard,
                  size_t nstandard, const RreqVendorFeature *vendor, size_t nvendor) {
    int ml = header->fuam.nCount;           /* ML: the length of every mask */
    FeatureCount nvf = nvendor;
    size_t start, i;

    if (out->error != NULL)
        return -1;
    if (header->nsf != nstandard)
        return out_fail(out, "Reader Requirements NSF other than the standard features given");
    if (header->dcm.nCount != ml)
        return out_fail(out, "Reader Requirements masks of different lengths (ML)");
    for (i = 0; i < nstandard; i++)
        if (standard[i].sm.nCount != ml)
            return out_fail(out, "Reader Requirements masks of different lengths (ML)");
    for (i = 0; i < nvendor; i++)
        if (vendor[i].vm.nCount != ml)
            return out_fail(out, "Reader Requirements masks of different lengths (ML)");
    if (hv_begin_box(out, HV_BOX_RREQ, 0, &start) != 0 || APPEND(RreqHeader, header, out) != 0)
        return -1;
    for (i = 0; i < nstandard; i++)
        if (APPEND(RreqStandardFeature, &standard[i], out) != 0)
            return -1;
    if (APPEND(FeatureCount, &nvf, out) != 0)
        return -1;
    for (i = 0; i < nvendor; i++)
        if (APPEND(RreqVendorFeature, &vendor[i], out) != 0)
            return -1;
    return hv_end_box(out, start);
}
