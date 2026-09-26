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
    static SizSegment_Std siz;
    static CodSegment_Std cod;
    static QcdSegment_Std qcd;
    static ComSegment_Std com;
    uint64_t *lengths = malloc(count * sizeof *lengths);
    size_t i, start;
    int status;

    if (lengths == NULL)
        return -1;
    for (i = 0; i < count; i++)
        lengths[i] = 1;
    memset(&siz, 0, sizeof siz);
    siz.body.xsiz = siz.body.ysiz = siz.body.xtsiz = siz.body.ytsiz = 8;
    siz.body.csiz = 1;
    siz.body.components.nCount = 1;
    siz.body.components.arr[0].depthMinus1 = 7;
    siz.body.components.arr[0].xrsiz = siz.body.components.arr[0].yrsiz = 1;
    memset(&cod, 0, sizeof cod);
    cod.body.sgcod.layers = 1;
    cod.body.spcod.cbWidthExp = cod.body.spcod.cbHeightExp = 4;
    cod.body.spcod.transform = 1;
    memset(&qcd, 0, sizeof qcd);
    qcd.body.sqcd = 0x40;
    qcd.body.spqcd.nCount = 1;
    qcd.body.spqcd.arr[0] = 0x48;
    memset(&com, 0, sizeof com);
    com.body.rcom = 1;
    com.body.ccom.nCount = 4;
    memcpy(com.body.ccom.arr, "test", 4);
    status = hv_write_marker(out, HV_SOC) || hv_write_siz(out, &siz) || hv_write_cod(out, &cod) ||
             hv_write_qcd(out, &qcd) || hv_write_com(out, &com) ||
             hv_begin_tile_part(out, 0, 0, 1, &start) || hv_write_plt(out, lengths, count) ||
             hv_write_marker(out, HV_SOD);
    for (i = 0; i < count && status == 0; i++)
        status = hv_write_bytes(out, "\x01", 1);
    if (status == 0)
        status = hv_end_tile_part(out, start) || hv_write_marker(out, HV_EOC);
    free(lengths);
    return status ? -1 : 0;
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
    check_errors();
    printf(failures ? "%d failures\n" : "all checks passed\n", failures);
    return failures != 0;
}
