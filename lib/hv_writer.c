/* hv_writer.c: see hv_writer.h. */
#include "hv_writer.h"

#include "hv_codes.h"
#include "jp2-boxes.h"        /* FragmentList-Profile */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* The largest box LBox can state; tests lower it to exercise XLBox. */
#ifndef HV_LBOX_MAX
#define HV_LBOX_MAX UINT32_MAX
#endif

static int fail(hv_out *out, const char *error) {
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

static int reserve(hv_out *out, size_t more) {
    size_t need, capacity;
    uint8_t *data;

    if (out->error != NULL)
        return -1;
    if (more > SIZE_MAX - out->size)
        return fail(out, "output too large");
    need = out->size + more;
    if (need <= out->capacity)
        return 0;
    capacity = out->capacity ? out->capacity : 4096;
    while (capacity < need)
        capacity = capacity > SIZE_MAX / 2 ? need : capacity * 2;
    if ((data = realloc(out->data, capacity)) == NULL)
        return fail(out, "out of memory");
    out->data = data;
    out->capacity = capacity;
    return 0;
}

/* Runs a generated encoder at `at`, with room for its largest encoding
 * (the generated encoders do not check the room themselves). They also
 * skip zero bits instead of writing them, so the `clear` bytes at `at` are
 * zeroed first: all of the room when appending, exactly the header being
 * replaced when patching one in place. */
#define ENCODE(T, value, out, at, clear, written)                            \
    encode_##T((value), (out), (at), (clear), (written))

#define DEFINE_ENCODE(T)                                                     \
    static int encode_##T(const T *value, hv_out *out, size_t at,           \
                          size_t clear, size_t *written) {                  \
        BitStream s;                                                         \
        int err = 0;                                                         \
        size_t room = T##_REQUIRED_BYTES_FOR_ACN_ENCODING;                   \
        if (at > out->size || (at + room > out->size &&                      \
                               reserve(out, at + room - out->size) != 0))   \
            return -1;                                                       \
        memset(out->data + at, 0, clear < room ? clear : room);              \
        BitStream_AttachBuffer(&s, out->data + at, (long)room);              \
        if (!T##_ACN_Encode(value, &s, &err, TRUE))                         \
            return fail(out, "invalid " #T);                                 \
        *written = (size_t)BitStream_GetLength(&s);                          \
        return 0;                                                            \
    }

DEFINE_ENCODE(BoxHeader)
DEFINE_ENCODE(MarkerCode)
DEFINE_ENCODE(SegmentLength)
DEFINE_ENCODE(SotSegment)
DEFINE_ENCODE(Rreq_Std)
DEFINE_ENCODE(SizSegment_Std)
DEFINE_ENCODE(CodSegment_Std)
DEFINE_ENCODE(QcdSegment_Std)
DEFINE_ENCODE(PltSegment_Std)
DEFINE_ENCODE(ComSegment_Std)
DEFINE_ENCODE(FragmentList_Profile)
DEFINE_ENCODE(DataReferenceCount)
DEFINE_ENCODE(UrlHeader)

/* Reads back a header this writer wrote, with the generated decoder. */
#define DECODE(T, value, out, at, size)                                      \
    decode_##T((value), (out), (at), (size))

#define DEFINE_DECODE(T)                                                     \
    static int decode_##T(T *value, hv_out *out, size_t at, size_t size) {  \
        BitStream s;                                                         \
        int err = 0;                                                         \
        if (at > out->size || size > out->size - at)                         \
            return fail(out, "no " #T " at the given offset");               \
        BitStream_AttachBuffer(&s, out->data + at, (long)size);              \
        if (!T##_ACN_Decode(value, &s, &err))                               \
            return fail(out, "no " #T " at the given offset");               \
        return 0;                                                            \
    }

DEFINE_DECODE(BoxHeader)
DEFINE_DECODE(SotSegment)

/* Appends a generated encoding. */
#define APPEND(T, value, out)                                                \
    append_##T((value), (out))

#define DEFINE_APPEND(T)                                                     \
    static int append_##T(const T *value, hv_out *out) {                    \
        size_t written;                                                      \
        if (ENCODE(T, value, out, out->size,                                 \
                   T##_REQUIRED_BYTES_FOR_ACN_ENCODING, &written) != 0)      \
            return -1;                                                       \
        out->size += written;                                                \
        return 0;                                                            \
    }

DEFINE_APPEND(BoxHeader)
DEFINE_APPEND(MarkerCode)
DEFINE_APPEND(SegmentLength)
DEFINE_APPEND(SotSegment)
DEFINE_APPEND(SizSegment_Std)
DEFINE_APPEND(CodSegment_Std)
DEFINE_APPEND(QcdSegment_Std)
DEFINE_APPEND(PltSegment_Std)
DEFINE_APPEND(ComSegment_Std)
DEFINE_APPEND(FragmentList_Profile)
DEFINE_APPEND(DataReferenceCount)
DEFINE_APPEND(UrlHeader)
DEFINE_APPEND(Rreq_Std)

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

int hv_write_segment(hv_out *out, uint16_t code, const uint8_t *body, size_t size) {
    SegmentLength l;
    if (size > UINT16_MAX - HV_FIXED(SegmentLength))      /* Lxxx counts itself */
        return fail(out, "marker segment body longer than 65 533 bytes");
    l = size + 2;
    if (hv_write_marker(out, code) != 0 || APPEND(SegmentLength, &l, out) != 0)
        return -1;
    return hv_write_bytes(out, body, size);
}

int hv_write_siz(hv_out *out, const SizSegment_Std *siz) {
    return hv_write_marker(out, HV_SIZ) != 0 ? -1 : APPEND(SizSegment_Std, siz, out);
}

int hv_write_cod(hv_out *out, const CodSegment_Std *cod) {
    return hv_write_marker(out, HV_COD) != 0 ? -1 : APPEND(CodSegment_Std, cod, out);
}

int hv_write_qcd(hv_out *out, const QcdSegment_Std *qcd) {
    return hv_write_marker(out, HV_QCD) != 0 ? -1 : APPEND(QcdSegment_Std, qcd, out);
}

int hv_write_com(hv_out *out, const ComSegment_Std *com) {
    return hv_write_marker(out, HV_COM) != 0 ? -1 : APPEND(ComSegment_Std, com, out);
}

/* One Iplt entry: 7-bit groups, most significant first. Returns its bytes. */
static int set_iplt(Iplt *p, uint64_t value) {
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
    return n;
}

int hv_write_plt(hv_out *out, const uint64_t *lengths, size_t count) {
    PltSegment_Std *plt;
    size_t i = 0;
    int zplt = 0, status = 0;

    if (out->error != NULL)
        return -1;
    if ((plt = malloc(sizeof *plt)) == NULL)
        return fail(out, "out of memory");
    while (status == 0 && i < count) {
        size_t bytes = 0;
        if (zplt > UINT8_MAX) {                           /* Zplt, A.7.3 */
            status = fail(out, "more than 256 PLT segments in a tile-part");
            break;
        }
        plt->body.zplt = zplt++;
        plt->body.entries.nCount = 0;
        /* Lplt = its own 2 bytes + Zplt + entry bytes <= 65 535. */
        while (i < count) {
            Iplt entry;
            int n;
            if (lengths[i] == 0) {
                status = fail(out, "zero packet length");
                break;
            }
            n = set_iplt(&entry, lengths[i]);
            if (bytes + n > UINT16_MAX - HV_FIXED(SegmentLength) - 1)
                break;
            plt->body.entries.arr[plt->body.entries.nCount++] = entry;
            bytes += n;
            i++;
        }
        if (status == 0)
            status = hv_write_marker(out, HV_PLT) != 0 ? -1 : APPEND(PltSegment_Std, plt, out);
    }
    free(plt);
    return status;
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
        return fail(out, "tile-part shorter than SOT and SOD");
    length = out->size - start;
    if (length > UINT32_MAX)
        return fail(out, "tile-part longer than Psot can state");
    if (DECODE(SotSegment, &sot, out, start + MARKER, HV_FIXED(SotSegment)) != 0)
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
        return fail(out, "box shorter than its header");
    length = out->size - start;
    if (DECODE(BoxHeader, &h, out, start, length < HV_BOX_HEADER_XL ? length : HV_BOX_HEADER_XL) != 0)
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
    FragmentList_Profile f;
    memset(&f, 0, sizeof f);
    f.nf = 1;
    f.fragments.nCount = 1;
    f.fragments.arr[0].off = offset;
    f.fragments.arr[0].len = length;
    f.fragments.arr[0].dr = dr;
    return hv_write_box_header(out, HV_BOX_FLST, HV_FIXED(FragmentList_Profile)) != 0
         ? -1 : APPEND(FragmentList_Profile, &f, out);
}

int hv_write_ndr(hv_out *out, uint16_t ndr) {
    DataReferenceCount c = ndr;
    return APPEND(DataReferenceCount, &c, out);
}

int hv_write_url(hv_out *out, const char *loc) {
    UrlHeader h = {0, 0};
    size_t n = strlen(loc) + 1;
    if (hv_write_box_header(out, HV_BOX_URL, HV_FIXED(UrlHeader) + (uint64_t)n) != 0 ||
        APPEND(UrlHeader, &h, out) != 0)
        return -1;
    return hv_write_bytes(out, loc, n);
}

int hv_write_rreq(hv_out *out, const Rreq_Std *rreq) {
    size_t start;
    return hv_begin_box(out, HV_BOX_RREQ, 0, &start) != 0 || APPEND(Rreq_Std, rreq, out) != 0
         ? -1 : hv_end_box(out, start);
}
