/* test_writer: hv_writer, compiled here with a largest LBox of 100 bytes
 * instead of 4 GiB - 1, so that the switch to XLBox (hv_end_box,
 * hv_write_box_header) runs on small boxes. What the writer writes is read
 * back with hv_reader.
 *
 *   test_writer */
#define HV_LBOX_MAX 100u
#include "hv_writer.c"

#include <stdio.h>

#include "hv_reader.h"

static int failures;

static void check(int ok, const char *what, const char *detail) {
    if (!ok) {
        printf("FAIL %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
        failures++;
    }
}

/* The writer's invariant: the spare capacity past out->size is zero. */
static int spare_zero(const hv_out *out) {
    size_t i;
    for (i = out->size; i < out->capacity; i++)
        if (out->data[i] != 0)
            return 0;
    return 1;
}

/* A codestream with one 8 x 8 component, no decomposition and one
 * tile-part whose PLT lists `count` packets of one byte each. */
static int write_codestream(hv_out *out, size_t count) {
    static SizFixed fixed;
    static Component component;
    static CodSegment_Std cod;
    static QcdSegment_Std qcd;
    hv_siz siz = {&fixed, &component, 1};
    uint64_t *lengths = malloc(count * sizeof *lengths);
    size_t i, start;
    int status;

    if (lengths == NULL)
        return -1;
    for (i = 0; i < count; i++)
        lengths[i] = 1;
    memset(&fixed, 0, sizeof fixed);
    fixed.xsiz = fixed.ysiz = fixed.xtsiz = fixed.ytsiz = 8;
    fixed.csiz = 1;
    memset(&component, 0, sizeof component);
    component.depthMinus1 = 7;
    component.xrsiz = component.yrsiz = 1;
    memset(&cod, 0, sizeof cod);
    cod.body.sgcod.layers = 1;
    cod.body.spcod.cbWidthExp = cod.body.spcod.cbHeightExp = 4;
    cod.body.spcod.transform = 1;
    memset(&qcd, 0, sizeof qcd);
    qcd.body.sqcd = 0x40;
    qcd.body.spqcd.nCount = 1;
    qcd.body.spqcd.arr[0] = 0x48;
    status = hv_write_marker(out, HV_SOC) || hv_write_siz(out, &siz) || hv_write_cod(out, &cod) ||
             hv_write_qcd(out, &qcd) || hv_write_com(out, 1, (const uint8_t *)"test", 4) ||
             hv_begin_tile_part(out, 0, 0, 1, &start) || hv_write_plt(out, lengths, count) ||
             hv_write_marker(out, HV_SOD);
    for (i = 0; i < count && status == 0; i++)
        status = hv_write_bytes(out, "\x01", 1);
    if (status == 0)
        status = hv_end_tile_part(out, start) || hv_write_marker(out, HV_EOC);
    free(lengths);
    return status ? -1 : 0;
}

/* What write_codestream wrote element by element, read back item by item:
 * SIZ and its one component, COM and its text, and the PLT split the way
 * Kakadu splits it, each Lxxx measured. */
static void check_elements(const hv_out *out, size_t count) {
    hv_codestream cs;
    hv_item item;
    size_t entries = 0, segments = 0, first = 0;
    int status, siz = 0, com = 0;

    if (hv_codestream_open(&cs, out->data, 0, out->size, 0) != 0) {
        check(0, "the codestream opens", cs.error);
        hv_codestream_close(&cs);
        return;
    }
    while ((status = hv_codestream_next(&cs, &item)) == 1) {
        if (item.siz != NULL)
            siz = item.siz == hv_codestream_siz(&cs) && item.siz->ncomponents == 1 &&
                  item.siz->fixed->csiz == 1 && item.siz->components[0].depthMinus1 == 7 &&
                  item.end - item.start == 2 + 41;      /* the code, Lsiz 38 + 3 */
        if (item.com != NULL)
            com = item.com->rcom == 1 && item.com->size == 4 &&
                  memcmp(item.com->text, "test", 4) == 0 &&
                  item.com->text == out->data + item.end - 4;
        if (item.plt != NULL) {
            size_t pos = item.plt->start, n = 0;
            uint64_t v;
            int more;
            while ((more = hv_plt_next(out->data, &pos, item.plt->end, &v)) == 1 && v == 1)
                n++;
            check(more == 0 && pos == item.end && item.plt->end == item.end &&
                  item.plt->start == item.start + 5 && item.plt->zplt == segments,
                  "PLT entries read back", NULL);
            if (segments++ == 0)
                first = n;
            entries += n;
        }
    }
    check(status == 0, "the codestream reads back item by item", cs.error);
    check(siz, "SIZ read back", NULL);
    check(com, "COM read back, its text in place", NULL);
    /* Lplt 65,535: its own 2 bytes, Zplt, and 65,532 one-byte entries. */
    check(segments == 2 && first == 65532 && entries == count, "PLT split at Lplt 65,535",
          NULL);
    hv_codestream_close(&cs);
}

/* Every write leaves the spare capacity zero, and appending after
 * hv_out_rewind over nonzero bytes gives what a fresh hv_out gives: the
 * generated encoders skip zero bits instead of writing them. */
static void check_spare_capacity(void) {
    hv_out fresh, reused;
    uint8_t ones[70000];
    const char *error;

    hv_out_init(&fresh);
    hv_out_init(&reused);
    memset(ones, 0xFF, sizeof ones);
    check(hv_write_bytes(&reused, ones, sizeof ones) == 0 && spare_zero(&reused),
          "spare capacity after hv_write_bytes", reused.error);
    hv_out_rewind(&reused, 1);
    check(reused.size == 1 && spare_zero(&reused), "spare capacity after hv_out_rewind", NULL);
    hv_out_rewind(&reused, 0);
    check(write_codestream(&fresh, 70000) == 0, "codestream written", fresh.error);
    check(write_codestream(&reused, 70000) == 0, "codestream written after a rewind",
          reused.error);
    check(spare_zero(&fresh) && spare_zero(&reused), "spare capacity after the encoders", NULL);
    check(fresh.size == reused.size && memcmp(fresh.data, reused.data, fresh.size) == 0,
          "a rewound hv_out writes what a fresh one does", NULL);
    /* Two PLT segments (Zplt 0 and 1), Psot, and the PLT sums. */
    error = hv_codestream_check(fresh.data, 0, fresh.size, 0, &(size_t){0});
    check(error == NULL, "the codestream reads back", error);
    check_elements(&fresh, 70000);
    hv_out_free(&fresh);
    hv_out_free(&reused);
}

/* The first box of buf[0, size), in *box: 1 if there is one. */
static int read_box(const uint8_t *buf, size_t size, hv_box *box) {
    hv_boxes it;
    const char *error;
    size_t at;
    hv_boxes_file(&it, buf, size);
    return hv_boxes_next(&it, box, &error, &at) == 1;
}

static void check_xlbox(void) {
    hv_out out;
    hv_box box, inner;
    hv_boxes it;
    uint8_t payload[200];
    size_t start, inner_start, i, at;
    const char *error;
    int same = 1;

    for (i = 0; i < sizeof payload; i++)
        payload[i] = (uint8_t)(i + 1);

    /* A box of exactly HV_LBOX_MAX bytes keeps LBox. */
    hv_out_init(&out);
    check(hv_begin_box(&out, HV_BOX_XML, 0, &start) == 0 &&
          hv_write_bytes(&out, payload, HV_LBOX_MAX - HV_BOX_HEADER) == 0 &&
          hv_end_box(&out, start) == 0 && out.size == HV_LBOX_MAX && read_box(out.data,
          out.size, &box) && box.payload == HV_BOX_HEADER && box.end == HV_LBOX_MAX,
          "a box of HV_LBOX_MAX bytes", out.error);
    hv_out_free(&out);

    /* One byte more: XLBox, the payload moved up 8 bytes and intact. */
    hv_out_init(&out);
    check(hv_begin_box(&out, HV_BOX_XML, 0, &start) == 0 &&
          hv_write_bytes(&out, payload, HV_LBOX_MAX - HV_BOX_HEADER + 1) == 0 &&
          hv_end_box(&out, start) == 0 && read_box(out.data, out.size, &box) &&
          box.type == HV_BOX_XML && box.payload == HV_BOX_HEADER_XL &&
          box.end == out.size && out.size == HV_LBOX_MAX + 1 + HV_BOX_HEADER_XL - HV_BOX_HEADER &&
          memcmp(out.data + box.payload, payload, HV_LBOX_MAX - HV_BOX_HEADER + 1) == 0 &&
          spare_zero(&out), "a box switched to XLBox", out.error);
    hv_out_free(&out);

    /* Begun extended: XLBox from the start. */
    hv_out_init(&out);
    check(hv_begin_box(&out, HV_BOX_XML, 1, &start) == 0 &&
          hv_write_bytes(&out, payload, 10) == 0 && hv_end_box(&out, start) == 0 &&
          read_box(out.data, out.size, &box) && box.payload == HV_BOX_HEADER_XL &&
          box.end == HV_BOX_HEADER_XL + 10, "a box begun extended", out.error);
    hv_out_free(&out);

    /* Nested: the inner box switches first, then the outer one. */
    hv_out_init(&out);
    check(hv_begin_box(&out, HV_BOX_JP2H, 0, &start) == 0 &&
          hv_begin_box(&out, HV_BOX_XML, 0, &inner_start) == 0 &&
          hv_write_bytes(&out, payload, sizeof payload) == 0 &&
          hv_end_box(&out, inner_start) == 0 && hv_end_box(&out, start) == 0,
          "nested boxes written", out.error);
    if (read_box(out.data, out.size, &box) && box.type == HV_BOX_JP2H &&
        box.payload == HV_BOX_HEADER_XL && box.end == out.size) {
        hv_boxes_children(&it, out.data, &box);
        same = hv_boxes_next(&it, &inner, &error, &at) == 1 && inner.type == HV_BOX_XML &&
               inner.payload == box.payload + HV_BOX_HEADER_XL && inner.end == box.end &&
               memcmp(out.data + inner.payload, payload, sizeof payload) == 0;
    } else {
        same = 0;
    }
    check(same, "nested boxes switched to XLBox", NULL);
    hv_out_free(&out);

    /* hv_write_box_header: XLBox for a payload above HV_LBOX_MAX - 8. */
    hv_out_init(&out);
    check(hv_write_box_header(&out, HV_BOX_XML, HV_LBOX_MAX - HV_BOX_HEADER) == 0 &&
          out.size == HV_BOX_HEADER, "box header with LBox", out.error);
    hv_out_rewind(&out, 0);
    check(hv_write_box_header(&out, HV_BOX_XML, HV_LBOX_MAX - HV_BOX_HEADER + 1) == 0 &&
          out.size == HV_BOX_HEADER_XL &&
          hv_write_bytes(&out, payload, HV_LBOX_MAX - HV_BOX_HEADER + 1) == 0 &&
          read_box(out.data, out.size, &box) && box.payload == HV_BOX_HEADER_XL &&
          box.end == out.size, "box header with XLBox", out.error);
    hv_out_free(&out);

    /* A whole codestream in a jp2c box that switches to XLBox. */
    hv_out_init(&out);
    check(hv_begin_box(&out, HV_BOX_JP2C, 0, &start) == 0 && write_codestream(&out, 3) == 0 &&
          hv_end_box(&out, start) == 0 && read_box(out.data, out.size, &box) &&
          box.payload == HV_BOX_HEADER_XL &&
          hv_codestream_check(out.data, box.payload, box.end, 0, &at) == NULL,
          "codestream in a jp2c box with XLBox", out.error);
    hv_out_free(&out);
}

/* PLT entries of every size, 1 to 10 bytes, each encoded on its own, read
 * back with hv_plt_next. */
static void check_plt_entries(void) {
    uint64_t lengths[11], v;
    hv_out out;
    size_t i, pos = 5;          /* after the code, Lplt and Zplt */
    int ok = 1;

    for (i = 0; i < 10; i++)
        lengths[i] = (uint64_t)1 << (7 * i);         /* i + 1 bytes */
    lengths[10] = UINT64_MAX;                        /* 10 bytes */
    hv_out_init(&out);
    check(hv_write_plt(&out, lengths, 11) == 0 && out.size == 5 + 55 + 10 &&
          out.data[2] == 0 && out.data[3] == 68 && out.data[4] == 0, "PLT of 11 entries",
          out.error);
    for (i = 0; i < 11 && ok; i++)
        ok = hv_plt_next(out.data, &pos, out.size, &v) == 1 && v == lengths[i];
    check(ok && hv_plt_next(out.data, &pos, out.size, &v) == 0,
          "PLT entries of 1 to 10 bytes read back", NULL);
    hv_out_free(&out);
}

/* The JPX boxes, element by element with LBox measured, byte for byte:
 * flst (NF 1 and a Fragment), url (VERS, FLAG, LOC and its NUL) and rreq
 * (RreqHeader with ML inserted, the standard features, NVF, the vendor
 * features). */
static void check_jpx_boxes(void) {
    static const uint8_t flst[] = {
        0, 0, 0, 24, 'f', 'l', 's', 't', 0, 1,
        0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 3, 4, 0, 5};
    static const uint8_t url[] = {
        0, 0, 0, 15, 'u', 'r', 'l', ' ', 0, 0, 0, 0, 'a', 'b', 0};
    static const uint8_t rreq[] = {
        0, 0, 0, 39, 'r', 'r', 'e', 'q', 2, 0x80, 0x00, 0x40, 0x00, 0, 1,
        0, 5, 0x80, 0x00, 0, 1,
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0x40, 0x00};
    RreqHeader header;
    RreqStandardFeature standard;
    RreqVendorFeature vendor;
    hv_out out;
    size_t i;

    hv_out_init(&out);
    check(hv_write_flst(&out, 0x102, 0x304, 5) == 0 && out.size == sizeof flst &&
          memcmp(out.data, flst, sizeof flst) == 0, "flst box", out.error);
    hv_out_rewind(&out, 0);
    check(hv_write_url(&out, "ab") == 0 && out.size == sizeof url &&
          memcmp(out.data, url, sizeof url) == 0, "url box", out.error);
    hv_out_rewind(&out, 0);

    memset(&header, 0, sizeof header);
    memset(&standard, 0, sizeof standard);
    memset(&vendor, 0, sizeof vendor);
    header.fuam.nCount = header.dcm.nCount = 2;
    header.fuam.arr[0] = 0x80;
    header.dcm.arr[0] = 0x40;
    header.nsf = 1;
    standard.sf = 5;
    standard.sm.nCount = 2;
    standard.sm.arr[0] = 0x80;
    for (i = 0; i < 16; i++)
        vendor.vf.arr[i] = (byte)(i + 1);
    vendor.vm.nCount = 2;
    vendor.vm.arr[0] = 0x40;
    check(hv_write_rreq(&out, &header, &standard, 1, &vendor, 1) == 0 &&
          out.size == sizeof rreq && memcmp(out.data, rreq, sizeof rreq) == 0, "rreq box",
          out.error);
    hv_out_rewind(&out, 0);
    check(hv_write_rreq(&out, &header, &standard, 0, NULL, 0) != 0 && out.error != NULL &&
          strcmp(out.error, "Reader Requirements NSF other than the standard features given")
          == 0 && out.size == 0, "rreq NSF against the features", out.error);
    hv_out_rewind(&out, 0);
    vendor.vm.nCount = 1;
    check(hv_write_rreq(&out, &header, &standard, 1, &vendor, 1) != 0 && out.error != NULL &&
          strcmp(out.error, "Reader Requirements masks of different lengths (ML)") == 0 &&
          out.size == 0, "rreq masks of different lengths", out.error);
    hv_out_free(&out);
}

/* Errors: the message, sticky until hv_out_rewind. */
static void check_errors(void) {
    static uint8_t body[65534];
    uint64_t lengths[2] = {1, 0};
    hv_out out;
    size_t start;

    hv_out_init(&out);
    check(hv_write_segment(&out, HV_COM, body, sizeof body) != 0 && out.error != NULL &&
          strcmp(out.error, "marker segment body longer than 65,533 bytes") == 0,
          "segment body too long", out.error);
    check(hv_write_marker(&out, HV_SOC) != 0 && out.size == 0, "writes after an error", NULL);
    hv_out_rewind(&out, 0);
    check(out.error == NULL && hv_write_segment(&out, HV_COM, body, sizeof body - 1) == 0 &&
          out.size == sizeof body - 1 + 4, "longest segment body", out.error);
    hv_out_rewind(&out, 0);
    check(hv_write_plt(&out, lengths, 2) != 0 && out.error != NULL &&
          strcmp(out.error, "zero packet length") == 0, "zero packet length", out.error);
    hv_out_rewind(&out, 0);
    check(hv_write_com(&out, 1, body, 0) != 0 && out.error != NULL &&
          strcmp(out.error, "COM segment without text") == 0, "COM without text", out.error);
    hv_out_rewind(&out, 0);
    /* Lcom 65,535: its own 2 bytes, Rcom and 65,531 bytes of text. */
    check(hv_write_com(&out, 0, body, sizeof body - 3) == 0 && out.size == 2 + 65535 &&
          hv_write_com(&out, 0, body, sizeof body - 2) != 0 && out.error != NULL &&
          strcmp(out.error, "marker segment longer than 65,535 bytes (Lxxx)") == 0,
          "longest COM text, and one byte more", out.error);
    hv_out_rewind(&out, 0);
    check(hv_end_tile_part(&out, 1) != 0 && out.error != NULL &&
          strcmp(out.error, "tile-part shorter than SOT and SOD") == 0,
          "tile-part end without a tile-part", out.error);
    hv_out_rewind(&out, 0);
    check(hv_begin_box(&out, HV_BOX_XML, 0, &start) == 0 && hv_end_box(&out, start + 1) != 0 &&
          strcmp(out.error, "box shorter than its header") == 0, "box end without a box",
          out.error);
    hv_out_free(&out);
}

int main(void) {
    check_spare_capacity();
    check_xlbox();
    check_plt_entries();
    check_jpx_boxes();
    check_errors();
    printf(failures ? "%d failures\n" : "all checks passed\n", failures);
    return failures != 0;
}
