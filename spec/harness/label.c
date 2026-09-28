/* label.c — see label.h. */
#include "label.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asn1crt.h"
#include "asn1crt_encoding.h"
#include "asn1crt_encoding_acn.h"

static uint32_t be32(const unsigned char *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

int children_offset(uint32_t type, int depth, BoxLayer layer) {
    if (layer == LAYER_PROFILE_JP2) return -1;
    if (layer == LAYER_STANDARD_JP2)
        return (depth == 0 && type == HV_BOX_JP2H) || (depth == 1 && type == HV_BOX_RES) ? 0 : -1;
    if (depth > 0)
        return depth == 1 && (type == HV_BOX_RES || type == HV_BOX_CGRP) &&
               layer == LAYER_STANDARD ? 0 : -1;
    switch (type) {
        case HV_BOX_JPCH: case HV_BOX_FTBL:
            return 0;
        case HV_BOX_DTBL:
            return 2;                                   /* NDR */
        case HV_BOX_JP2H: case HV_BOX_JPLH: case HV_BOX_UINF: case HV_BOX_ASOC:
            return layer == LAYER_STANDARD ? 0 : -1;
        default:
            return -1;
    }
}

static Jp2Family *dec_family;            /* reused decode targets */
static Jp2File *dec_jp2_std;
static Jp2File_Profile *dec_jp2;
static JpxFile_Profile *dec_jpx;

static void *xcalloc(size_t n) {
    void *p = calloc(1, n);
    if (!p) {
        fprintf(stderr, "label: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

void label_init(void) {
    dec_family = xcalloc(sizeof *dec_family);
    dec_jp2_std = xcalloc(sizeof *dec_jp2_std);
    dec_jp2 = xcalloc(sizeof *dec_jp2);
    dec_jpx = xcalloc(sizeof *dec_jpx);
}

static struct {
    const char *url;
    uint64_t offset;
    uint32_t length;
} companions[16];
static int companion_count;

void label_companion(const char *url, uint64_t offset, uint32_t length) {
    if (companion_count == (int) (sizeof companions / sizeof *companions)) {
        fprintf(stderr, "label: more than 16 companions\n");
        exit(EXIT_FAILURE);
    }
    companions[companion_count].url = url;
    companions[companion_count].offset = offset;
    companions[companion_count++].length = length;
}

void label_companions_clear(void) {
    companion_count = 0;
}

static const char *check_linked_extents(const JpxFile_Profile *file) {
    const DataReferences_Profile *references = NULL;
    int i, j, k;
    for (i = 0; i < file->boxes.nCount; ++i)
        if (file->boxes.arr[i].payload.kind == TopPayload_Profile_dtbl_PRESENT)
            references = &file->boxes.arr[i].payload.u.dtbl;
    if (references == NULL) return NULL; /* embedded JPX */
    for (i = 0; i < file->boxes.nCount; ++i) {
        const TopBox_Profile *box = &file->boxes.arr[i];
        if (box->payload.kind != TopPayload_Profile_ftbl_PRESENT) continue;
        for (k = 0; k < box->payload.u.ftbl.children.nCount; ++k) {
            const InnerBox_Profile *child = &box->payload.u.ftbl.children.arr[k];
            const Fragment *fragment;
            const char *url;
            if (child->payload.kind != InnerPayload_Profile_flst_PRESENT) continue;
            /* Structural profile checks already required one external
             * fragment with an in-range DR. */
            fragment = &child->payload.u.flst.fragments.arr[0];
            url = (const char *) references->references.arr[fragment->dr - 1].payload.u.url.loc;
            for (j = 0; j < companion_count; ++j)
                if (strcmp(url, companions[j].url) == 0) break;
            if (j == companion_count) return "url.missing-companion";
            if (fragment->off != companions[j].offset || fragment->len != companions[j].length)
                return "flst.source-extent";
        }
    }
    return NULL;
}

/* The generated CONTAINING decoder temporarily trusts LBox as its stream
 * size. Check physical box boundaries first so a malformed LBox cannot make
 * an opaque unknown box read beyond the input buffer: those of every box the
 * layer reads (children_offset). A failure is reason `decode`. */
static int box_bounds_ok(const unsigned char *data, size_t start, size_t end, BoxLayer layer,
                         int depth) {
    while (start < end) {
        uint32_t length, type;
        size_t box_end;
        int offset;
        if (end - start < HV_BOX_HEADER) return 0;
        length = be32(data + start);
        type = be32(data + start + 4);
        if (length < HV_BOX_HEADER || length > end - start) return 0;
        box_end = start + length;
        if ((offset = children_offset(type, depth, layer)) >= 0) {
            size_t child_start = start + HV_BOX_HEADER + (size_t) offset;
            if (child_start > box_end || !box_bounds_ok(data, child_start, box_end, layer, depth + 1))
                return 0;
        }
        start = box_end;
    }
    return 1;
}

Label label(const unsigned char *data, size_t len, cf_kind kind) {
    Label l = { 0, 0, NULL, NULL };
    BitStream bs;
    hv_ftyp ftyp;
    int err = 0, std_jpx;
    const char *r;

    /* Layer 1: the kind the ftyp brand gives, or the extension's where
     * there is no ftyp to read (the file rules then fail). */
    if (hv_ftyp_read(data, len, &ftyp) == 0) std_jpx = hv_ftyp_is_jpx(&ftyp);
    else std_jpx = kind == CF_JPX;
    BitStream_AttachBuffer(&bs, (unsigned char *) data, (long) len);
    if (std_jpx) {
        Jp2Family_Initialize(dec_family);
        if (!box_bounds_ok(data, 0, len, LAYER_STANDARD, 0) ||
            !Jp2Family_ACN_Decode(dec_family, &bs, &err)) l.std_reason = "decode";
        else if ((r = cf_check_jpx(dec_family, data, len)) != NULL) l.std_reason = r;
        else l.std_ok = 1;
    } else {
        Jp2File_Initialize(dec_jp2_std);
        if (!box_bounds_ok(data, 0, len, LAYER_STANDARD_JP2, 0) ||
            !Jp2File_ACN_Decode(dec_jp2_std, &bs, &err)) l.std_reason = "decode";
        else if ((r = cf_check_jp2(dec_jp2_std, data, len)) != NULL) l.std_reason = r;
        else l.std_ok = 1;
    }

    BitStream_AttachBuffer(&bs, (unsigned char *) data, (long) len);
    if (kind == CF_JP2) {
        Jp2File_Profile_Initialize(dec_jp2);
        if (!box_bounds_ok(data, 0, len, LAYER_PROFILE_JP2, 0) ||
            !Jp2File_Profile_ACN_Decode(dec_jp2, &bs, &err)) l.prof_reason = "decode";
        else if ((r = cf_check_jp2_profile(dec_jp2)) != NULL) l.prof_reason = r;
        else l.prof_ok = 1;
    } else {
        JpxFile_Profile_Initialize(dec_jpx);
        if (!box_bounds_ok(data, 0, len, LAYER_PROFILE_JPX, 0) ||
            !JpxFile_Profile_ACN_Decode(dec_jpx, &bs, &err)) l.prof_reason = "decode";
        else if ((r = cf_check_jpx_profile(dec_jpx)) != NULL) l.prof_reason = r;
        else if ((r = check_linked_extents(dec_jpx)) != NULL) l.prof_reason = r;
        else l.prof_ok = 1;
    }
    return l;
}
