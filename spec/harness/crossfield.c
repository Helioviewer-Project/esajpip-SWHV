/* crossfield.c — instantiates crossfield_impl.h for both struct families,
 * checks the placement of boxes in the metadata superboxes and the JP2
 * header boxes at layer 1 (the only layer that types them), and checks a
 * .jp2 at layer 2, whose box types differ (Jp2File_Profile). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "crossfield.h"

/* Tile-part counts per tile, for hv_rule_tile_part: Isot addresses at most
 * 65,535 tiles. */
static uint16_t cf_tile_parts[65535];
static uint8_t cf_tile_tnsot[65535];

/* T.800 I.5.1: the signature box's contents. */
static int cf_signature(const OpaqueBox *box) {
    return box->data.nCount == 4 && memcmp(box->data.arr, "\x0D\x0A\x87\x0A", 4) == 0;
}

/* T.800 I.5.2 / T.801 Annex M: the brand for the file's kind, and in the
 * compatibility list. */
static const char *cf_ftyp(const Ftyp *ftyp, cf_kind kind) {
    Brand expected = kind == CF_JP2 ? HV_BRAND_JP2 : HV_BRAND_JPX;
    int i, compatible = 0;
    if (ftyp->header.brand != expected) return "file.ftyp-brand";
    for (i = 0; i < ftyp->compat.nCount; ++i)
        compatible |= ftyp->compat.arr[i] == expected;
    return compatible ? NULL : "file.ftyp-compatibility";
}

/* Layer-1 family: boxes have `other`; codestream markers do not. */
#define CF_S
#define CF_FILE Jp2Family
#define CF_FN cf_check_family_
#include "crossfield_impl.h"
#undef CF_S
#undef CF_FILE
#undef CF_FN

/* Layer-2 family for a .jpx: *_Profile types, whose codestream has the
 * `other` marker alternative and whose box tree shares the standard
 * layer's `other` boxes. */
#define CF_S _Profile
#define CF_FILE JpxFile_Profile
#define CF_FN cf_check_profile_common_
#define CF_COD Cod_Profile
#define CF_MAIN_OTHER
#define CF_TILE_PLT_ONLY     /* TileSegment-Profile has the plt alternative only */
#include "crossfield_impl.h"
#undef CF_S
#undef CF_FILE
#undef CF_FN
#undef CF_COD
#undef CF_MAIN_OTHER
#undef CF_TILE_PLT_ONLY

/* The children of a top-level superbox that the server does not walk
 * (jp2h, jplh, uinf, asoc; layer 1 only, as layer 2 keeps these boxes
 * opaque): jp2c, jpch, ftbl and dtbl belong at the top level and flst in
 * an ftbl (hv_rule_child). A url is not checked here: T.800 I.7.3 puts one
 * in uinf. */
static const char *cf_placement_children(uint32_t parent, const InnerBox *children, int n) {
    int i;
    const char *r;
    for (i = 0; i < n; ++i) {
        uint32_t type = cf_inner_type_(&children[i]);
        if (type != HV_BOX_URL && (r = hv_rule_child(parent, type)) != NULL) return r;
    }
    return NULL;
}

static const char *cf_placement(const Jp2Family *file) {
    int i;
    const char *r = NULL;
    for (i = 0; i < file->boxes.nCount && r == NULL; ++i) {
        const TopPayload *p = &file->boxes.arr[i].payload;
        switch (p->kind) {
            case TopPayload_jp2h_PRESENT:
                r = cf_placement_children(HV_BOX_JP2H, p->u.jp2h.children.arr,
                                          p->u.jp2h.children.nCount);
                break;
            case TopPayload_jplh_PRESENT:
                r = cf_placement_children(HV_BOX_JPLH, p->u.jplh.children.arr,
                                          p->u.jplh.children.nCount);
                break;
            case TopPayload_uinf_PRESENT:
                r = cf_placement_children(HV_BOX_UINF, p->u.uinf.children.arr,
                                          p->u.uinf.children.nCount);
                break;
            case TopPayload_asoc_PRESENT:
                r = cf_placement_children(HV_BOX_ASOC, p->u.asoc.children.arr,
                                          p->u.asoc.children.nCount);
                break;
            default:
                break;
        }
    }
    return r;
}

/* The header box type of a layer-1 inner box, as far as hv_rule_header_child
 * distinguishes them (1: any other). */
static uint32_t cf_header_type(const InnerBox *b) {
    switch (b->payload.kind) {
        case InnerPayload_ihdr_PRESENT: return HV_BOX_IHDR;
        case InnerPayload_bpcc_PRESENT: return HV_BOX_BPCC;
        case InnerPayload_colr_PRESENT: return HV_BOX_COLR;
        case InnerPayload_pclr_PRESENT: return HV_BOX_PCLR;
        case InnerPayload_cmap_PRESENT: return HV_BOX_CMAP;
        case InnerPayload_cdef_PRESENT: return HV_BOX_CDEF;
        case InnerPayload_res_PRESENT:  return HV_BOX_RES;
        case InnerPayload_jp2i_PRESENT: return HV_BOX_JP2I;
        case InnerPayload_creg_PRESENT: return HV_BOX_CREG;
        default:                        return 1;
    }
}

/* A header box's state for hv_rules.c: hv_header, and its first bpcc's
 * entries as bytes, where hv_rule_bpcc leaves them. */
typedef struct {
    hv_header h;
    uint8_t bpcc[sizeof ((Bpcc *) 0)->depths.arr / sizeof ((Bpcc *) 0)->depths.arr[0]];
} cf_header_state;

/* One header box (jp2h, jpch or jplh), child by child (hv_rules.c). */
static const char *cf_header(const Superbox *sb, uint32_t parent, cf_kind kind,
                             cf_header_state *state) {
    hv_header *h = &state->h;
    int i, j;
    const char *r;
    hv_header_init(h, parent, kind == CF_JPX);
    for (i = 0; i < sb->children.nCount; ++i) {
        const InnerPayload *p = &sb->children.arr[i].payload;
        if ((r = hv_rule_header_child(h, cf_header_type(&sb->children.arr[i]))) != NULL) return r;
        switch (p->kind) {
            case InnerPayload_ihdr_PRESENT:
                r = hv_rule_ihdr(h, &p->u.ihdr);
                break;
            case InnerPayload_bpcc_PRESENT: {
                /* The first bpcc's entries stay in state; a later one's
                 * (header.one-bpcc) never reach here. */
                for (j = 0; j < p->u.bpcc.depths.nCount; ++j)
                    state->bpcc[j] = (uint8_t) p->u.bpcc.depths.arr[j];
                r = hv_rule_bpcc(h, state->bpcc, (size_t) p->u.bpcc.depths.nCount);
                break;
            }
            case InnerPayload_colr_PRESENT:
                r = hv_rule_colr(h, &p->u.colr.header, (size_t) p->u.colr.rest.nCount);
                break;
            case InnerPayload_pclr_PRESENT: {
                const PclrHeader *ph = &p->u.pclr.header;
                r = hv_rule_pclr(h, ph->ne, (uint64_t) ph->depths.nCount);
                for (j = 0; j < ph->depths.nCount && r == NULL; ++j)
                    r = hv_rule_pclr_column(h, ph->depths.arr[j]);
                if (r == NULL) r = hv_rule_pclr_end(h, (uint64_t) p->u.pclr.entries.nCount);
                break;
            }
            case InnerPayload_cmap_PRESENT:
                for (j = 0; j < p->u.cmap.entries.nCount && r == NULL; ++j)
                    r = hv_rule_cmap_entry(h, &p->u.cmap.entries.arr[j]);
                break;
            case InnerPayload_cdef_PRESENT:
                r = hv_rule_cdef(h, p->u.cdef.entries.arr, (size_t) p->u.cdef.entries.nCount);
                if (r == NULL) r = hv_rule_extent(HV_BOX_CDEF, (uint64_t) p->u.cdef.extra.nCount);
                break;
            case InnerPayload_res_PRESENT:
                for (j = 0; j < p->u.res.children.nCount && r == NULL; ++j) {
                    const ResPayload *c = &p->u.res.children.arr[j].payload;
                    r = hv_rule_res_child(h, c->kind == resc_PRESENT ? HV_BOX_RESC
                                           : c->kind == resd_PRESENT ? HV_BOX_RESD : 1);
                    if (r == NULL && c->kind == resc_PRESENT)
                        r = hv_rule_extent(HV_BOX_RESC, (uint64_t) c->u.resc.extra.nCount);
                    if (r == NULL && c->kind == resd_PRESENT)
                        r = hv_rule_extent(HV_BOX_RESD, (uint64_t) c->u.resd.extra.nCount);
                }
                if (r == NULL) r = hv_rule_res_end(h);
                break;
            default:
                break;
        }
        if (r != NULL) return r;
    }
    return NULL;
}

/* The header boxes of the file, T.800 I.5.3 and T.801 M.11.5 to M.11.7:
 * where jp2h is, what each header box holds, and each codestream's header
 * against its SIZ (a linked codestream's SIZ is in another file). */
static const char *cf_headers(const Jp2Family *file, cf_kind kind) {
    static cf_header_state jp2h, jpch, jplh;
    const Superbox *jp2h_box = NULL;
    int i, n = 0, jp2c = 0, late = 0, codestreams = 0, headers = 0, stream = 0, jpchs = 0;
    int ipr = 0, jplhs = 0, cregs = 0;
    const char *r;

    /* T.801 M.8: MinV (the second box is ftyp, cf_check_family_). */
    if (kind == CF_JPX &&
        (r = hv_rule_ftyp_minor(file->boxes.arr[1].payload.u.ftyp.header.minor)) != NULL)
        return r;

    /* T.801 M.11.1: the Reader Requirements box, its count and place
     * checked by hv_rule_jpx. */
    for (i = 0; kind == CF_JPX && i < file->boxes.nCount; ++i)
        if (file->boxes.arr[i].payload.kind == TopPayload_rreq_PRESENT &&
            (r = hv_rule_extent(HV_BOX_RREQ, (uint64_t) file->boxes.arr[i].payload.u.rreq.extra.nCount)) != NULL)
            return r;

    for (i = 0; i < file->boxes.nCount; ++i) {
        switch (file->boxes.arr[i].payload.kind) {
            case TopPayload_jp2h_PRESENT:
                if (jp2h_box == NULL) jp2h_box = &file->boxes.arr[i].payload.u.jp2h;
                late |= codestreams > 0 || headers > 0;
                n++;
                break;
            case TopPayload_jp2c_PRESENT: jp2c++; codestreams++; break;
            case TopPayload_ftbl_PRESENT: codestreams += kind == CF_JPX; break;
            case TopPayload_jpch_PRESENT: jpchs++; headers++; break;
            case TopPayload_jplh_PRESENT: headers++; break;
            case TopPayload_jp2i_PRESENT: ipr++; break;
            default: break;
        }
        if (kind == CF_JP2) headers = 0;        /* T.800 I.2: before the jp2c box */
    }
    if ((r = hv_rule_jp2h_place(n, late, jp2c, kind == CF_JPX)) != NULL) return r;
    if (jp2h_box != NULL && (r = cf_header(jp2h_box, HV_BOX_JP2H, kind, &jp2h)) != NULL)
        return r;

    if (kind == CF_JP2) {
        for (i = 0; i < file->boxes.nCount; ++i)
            if (file->boxes.arr[i].payload.kind == TopPayload_jp2c_PRESENT) break;
        hv_siz siz = cf_siz_view(&file->boxes.arr[i].payload.u.jp2c.siz.body);
        if ((r = hv_rule_codestream_header(&jp2h.h, NULL, &siz, 0)) != NULL) return r;
        return hv_rule_ihdr_ipr(&jp2h.h, ipr);
    }

    /* JPX, in box order as the reader: each jplh; codestream k against
     * jpch k (M.11.6; the counts agree, hv_rule_jpx), or only jp2h's
     * defaults when there is no jpch. */
    for (i = 0; i < file->boxes.nCount; ++i) {
        const TopPayload *p = &file->boxes.arr[i].payload;
        hv_siz view;
        const hv_siz *siz = NULL;
        int k, seen = 0;
        if (p->kind == TopPayload_jplh_PRESENT) {
            if ((r = cf_header(&p->u.jplh, HV_BOX_JPLH, kind, &jplh)) != NULL) return r;
            jplhs++;
            cregs += jplh.h.creg > 0;
            continue;
        }
        if (p->kind != TopPayload_jp2c_PRESENT && p->kind != TopPayload_ftbl_PRESENT) continue;
        if (p->kind == TopPayload_jp2c_PRESENT) {
            view = cf_siz_view(&p->u.jp2c.siz.body);
            siz = &view;
        }
        if (jpchs == 0) {
            r = hv_rule_codestream_header(NULL, jp2h_box ? &jp2h.h : NULL, siz, 1);
        } else {
            for (k = 0; k < file->boxes.nCount; ++k)
                if (file->boxes.arr[k].payload.kind == TopPayload_jpch_PRESENT && seen++ == stream)
                    break;
            if ((r = cf_header(&file->boxes.arr[k].payload.u.jpch, HV_BOX_JPCH, kind, &jpch)) != NULL)
                return r;
            r = hv_rule_codestream_header(&jpch.h, jp2h_box ? &jp2h.h : NULL, siz, 1);
        }
        if (r != NULL) return r;
        stream++;
    }
    return hv_rule_jpx_creg(jplhs, cregs);
}

const char *cf_check_family(const Jp2Family *file, cf_layer layer, cf_kind kind) {
    const char *r = cf_check_family_(file, layer, kind);
    if (r != NULL || layer != CF_STANDARD) return r;
    return (r = cf_placement(file)) != NULL ? r : cf_headers(file, kind);
}

const char *cf_check_jpx_profile(const JpxFile_Profile *file) {
    return cf_check_profile_common_(file, CF_PROFILE, CF_JPX);
}

/* Layer 2 for a .jp2: ReadJP2 reads the signature, ftyp and jp2c boxes and
 * skips every other box, so the other boxes are opaque (Jp2Payload-Profile)
 * and only these rules apply. */
const char *cf_check_jp2_profile(const Jp2File_Profile *file) {
    int i, jp2c = 0;
    const char *r;
    if (file->boxes.nCount < 2) return "file.two-boxes";
    if (file->boxes.arr[0].payload.kind != Jp2Payload_Profile_jP_PRESENT ||
        !cf_signature(&file->boxes.arr[0].payload.u.jP))
        return "file.signature";
    if (file->boxes.arr[1].payload.kind != Jp2Payload_Profile_ftyp_PRESENT)
        return "file.ftyp-second";
    if ((r = cf_ftyp(&file->boxes.arr[1].payload.u.ftyp, CF_JP2)) != NULL) return r;
    for (i = 0; i < file->boxes.nCount; ++i) {
        if (file->boxes.arr[i].payload.kind != Jp2Payload_Profile_jp2c_PRESENT) continue;
        jp2c++;
        if ((r = cf_codestream_Profile(&file->boxes.arr[i].payload.u.jp2c, CF_PROFILE)) != NULL)
            return r;
    }
    return jp2c == 1 ? NULL : "jp2.one-codestream";
}
