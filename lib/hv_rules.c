/* hv_rules.c: see hv_rules.h. */
#include "hv_rules.h"

#include <stddef.h>
#include <string.h>

const char *hv_rule_siz(const hv_siz *siz, const Sgcod *sgcod, int profile) {
    const SizFixed *s = siz->fixed;
    const Component *k = siz->components;
    size_t i;
    if (s->csiz != siz->ncomponents) return "siz.csiz-count";
    if (!(s->xosiz < s->xsiz && s->yosiz < s->ysiz)) return "siz.origin-inside";
    if (!(s->xtosiz <= s->xosiz && s->ytosiz <= s->yosiz)) return "siz.tile-origin";
    if (!(s->xtosiz + s->xtsiz > s->xosiz && s->ytosiz + s->ytsiz > s->yosiz))
        return "siz.tile-covers-origin";
    if (sgcod != NULL && sgcod->mct != 0) {
        if (siz->ncomponents < 3) return "siz.mct-components";
        for (i = 1; i < 3; i++)
            if (k[i].depthMinus1 != k[0].depthMinus1 || k[i].xrsiz != k[0].xrsiz ||
                k[i].yrsiz != k[0].yrsiz)
                return "siz.mct-geometry";
    }
    if (profile) {
        if (s->xosiz != 0 || s->yosiz != 0 || s->xtosiz != 0 || s->ytosiz != 0)
            return "siz.zero-origin";
        for (i = 0; i < siz->ncomponents; i++)
            if (k[i].xrsiz != 1 || k[i].yrsiz != 1)
                return "siz.component-sampling";
        if (!(s->xtsiz >= s->xsiz && s->ytsiz >= s->ysiz)) return "siz.single-tile";
    }
    return NULL;
}

const char *hv_rule_cod(const Scod *scod, const Spcod *spcod, int profile) {
    int i;
    int expected = scod->customPrecincts ? (int)spcod->levels + 1 : 0;
    if (spcod->precincts.nCount != expected) return "cod.precincts-count";
    for (i = 1; i < spcod->precincts.nCount; i++)
        if (spcod->precincts.arr[i].ppx == 0 || spcod->precincts.arr[i].ppy == 0)
            return "cod.precincts-higher-zero";
    if (spcod->cbWidthExp + spcod->cbHeightExp > 8) return "cod.codeblock-area";
    if (profile && scod->sopMarkers) return "cod.sop-markers";
    return NULL;
}

uint32_t hv_rule_tiles(const hv_siz *siz) {
    const SizFixed *s = siz->fixed;
    uint64_t nx, ny;
    if (s->xsiz <= s->xtosiz || s->ysiz <= s->ytosiz || s->xtsiz == 0 || s->ytsiz == 0)
        return 0;
    nx = (s->xsiz - s->xtosiz + s->xtsiz - 1) / s->xtsiz;
    ny = (s->ysiz - s->ytosiz + s->ytsiz - 1) / s->ytsiz;
    return nx > 65535 || ny > 65535 || nx * ny > 65535 ? 65535 : (uint32_t)(nx * ny);
}

const char *hv_rule_tile_part(hv_tile_parts *t, uint64_t isot, uint64_t tpsot, uint64_t tnsot) {
    if (isot >= t->tiles) return "sot.isot-range";
    if (tpsot != t->parts[isot]) return "sot.tpsot-sequence";
    if (tnsot != 0) {
        if (tpsot >= tnsot) return "sot.tpsot-below-tnsot";
        if (t->tnsot[isot] != 0 && t->tnsot[isot] != tnsot) return "sot.tnsot-inconsistent";
        t->tnsot[isot] = (uint8_t)tnsot;
    }
    t->parts[isot]++;
    return NULL;
}

const char *hv_rule_tile_parts_end(const hv_tile_parts *t) {
    uint32_t i;
    for (i = 0; i < t->tiles; i++)
        if (t->tnsot[i] != 0 && t->parts[i] != t->tnsot[i]) return "sot.tnsot-count";
    return NULL;
}

const char *hv_rule_tile_part_count(uint64_t parts) {
    return parts > HV_PROFILE_TILE_PARTS ? "codestream.tile-part-limit" : NULL;
}

const char *hv_rule_iplt(const Iplt *e, uint64_t *value) {
    uint64_t v = e->b0.bits;
#define HV_IPLT_STEP(n)                                                      \
    if (e->exist.b##n) {                                                     \
        if (v > (UINT64_MAX >> 7)) return "plt.value-overflow";              \
        v = (v << 7) | (uint64_t)e->b##n.bits;                               \
    }
    HV_IPLT_STEP(1) HV_IPLT_STEP(2) HV_IPLT_STEP(3) HV_IPLT_STEP(4) HV_IPLT_STEP(5)
    HV_IPLT_STEP(6) HV_IPLT_STEP(7) HV_IPLT_STEP(8) HV_IPLT_STEP(9)
#undef HV_IPLT_STEP
    *value = v;
    return NULL;
}

const char *hv_rule_plt_entry(hv_plt_count *count, uint64_t value, int profile) {
    if (value == 0) {
        if (!profile) return "plt.zero-length";
        count->padding = 1;
    } else {
        if (count->padding) return "plt.padding-position";
        count->packets++;
    }
    return NULL;
}

uint64_t hv_rule_packets(const hv_siz *siz, const Sgcod *sgcod, const Spcod *spcod) {
    const SizFixed *s = siz->fixed;
    uint64_t total = 0;
    int levels = (int)spcod->levels, r;
    for (r = 0; r <= levels; r++) {
        uint64_t scale = (uint64_t)1 << (levels - r);
        uint64_t w = ((uint64_t)s->xsiz + scale - 1) / scale;   /* size at r */
        uint64_t h = ((uint64_t)s->ysiz + scale - 1) / scale;
        int ppx = 15, ppy = 15;
        if (spcod->precincts.nCount != 0) {
            ppx = (int)spcod->precincts.arr[r].ppx;
            ppy = (int)spcod->precincts.arr[r].ppy;
        }
        w = (w + ((uint64_t)1 << ppx) - 1) >> ppx;
        h = (h + ((uint64_t)1 << ppy) - 1) >> ppy;
        total += w * h;
        if (total > 2147483647u) return 0;
    }
    total *= (uint64_t)s->csiz;
    if (total > 2147483647u) return 0;
    total *= (uint64_t)sgcod->layers;
    return total > 2147483647u ? 0 : total;
}

const char *hv_rule_plt_packets(const hv_plt_count *count, const hv_siz *siz,
                                const Sgcod *sgcod, const Spcod *spcod, int profile) {
    uint64_t packets = hv_rule_packets(siz, sgcod, spcod);
    if (packets == 0)
        return profile ? "codestream.packet-count" : NULL;
    if (profile && count->padding && count->packets < packets) return "plt.zero-length";
    if (count->packets != packets) return "plt.packet-count";
    return NULL;
}

const char *hv_rule_child(uint32_t parent, uint32_t child) {
    if (parent == HV_BOX_DTBL && child != HV_BOX_URL) return "dtbl.non-url";
    if (child == HV_BOX_JP2C) return parent == HV_BOX_JPCH ? "jpch.nested-jp2c" : "box.nested-jp2c";
    if (child == HV_BOX_JPCH || child == HV_BOX_FTBL || child == HV_BOX_DTBL)
        return "box.nested-superbox";
    if (child == HV_BOX_FLST && parent != HV_BOX_FTBL) return "box.flst-placement";
    if (child == HV_BOX_URL && parent != HV_BOX_DTBL) return "box.url-placement";
    return NULL;
}

const char *hv_rule_url(uint64_t vers, uint64_t flag, const uint8_t *loc, size_t n) {
    static const char jp2[] = ".jp2";
    if (vers != 0 || flag != 0) return "url.version-flags";
    if (n == 0 || memchr(loc, 0, n) != loc + n - 1) return "url.terminator";
    n--;                                           /* the characters */
    if (n <= HV_FILE_SCHEME_LENGTH) return "url.length";   /* the scheme and a path */
    if (memcmp(loc, HV_FILE_SCHEME, HV_FILE_SCHEME_LENGTH) != 0) return "url.file-scheme";
    if (hv_url_path(loc + HV_FILE_SCHEME_LENGTH, n - HV_FILE_SCHEME_LENGTH, NULL, 0) != 0)
        return "url.percent-encoding";
    if (n < sizeof jp2 - 1 || memcmp(loc + n - (sizeof jp2 - 1), jp2, sizeof jp2 - 1) != 0)
        return "url.jp2-target";
    return NULL;
}

static int hex_digit(int c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int hv_url_path(const uint8_t *path, size_t n, char *out, size_t out_size) {
    size_t i, k = 0;
    for (i = 0; i < n; i++) {
        int c = path[i], hi, lo;
        if (c == '%') {
            if (n - i < 3 || (hi = hex_digit(path[i + 1])) < 0 ||
                (lo = hex_digit(path[i + 2])) < 0 || (c = hi * 16 + lo) == 0)
                return -1;
            i += 2;
        }
        if (out != NULL) {
            if (k + 1 >= out_size)
                return -2;
            out[k] = (char)c;
        }
        k++;
    }
    if (out != NULL) {
        if (out_size == 0)
            return -2;
        out[k] = 0;
    }
    return 0;
}

const char *hv_rule_flst(uint64_t nf, int fragments) {
    return nf == 1 && fragments == 1 ? NULL : "flst.one-fragment";
}

const char *hv_rule_fragment_dr(uint64_t dr, uint64_t ndr, int profile) {
    if (dr > ndr) return "flst.dr-range";
    if (profile && dr == 0) return "flst.dr-external";
    return NULL;
}

const char *hv_rule_jpx(const hv_jpx_boxes *b, int profile) {
    if (!profile && (b->rreq != 1 || !b->rreq_third)) return "jpx.reader-requirements";
    /* M.11.2: "A JPX file shall contain zero or one Data Reference boxes". */
    if (b->dtbl > 1) return "jpx.one-dtbl";
    /* M.11.6: as many codestreams (jp2c or ftbl) as codestream headers,
     * where there are any; without jpch, jp2h gives the header. */
    if (b->jpch > 0 && b->jp2c + b->ftbl != b->jpch) return "jpx.codestream-count";
    if (profile) {
        if (b->jpch < 1) return "jpx.no-jpch";     /* ReadJPX needs jpch */
        if (b->jp2c > 0 && b->ftbl > 0) return "jpx.mixed-sources";
        if (b->ftbl > 0 && b->dtbl != 1) return "jpx.linked-shape";
    }
    return NULL;
}

/* ------------------------------------------------------------------------
 * JP2 header boxes
 * ------------------------------------------------------------------------ */

const char *hv_rule_jp2h_place(int jp2h, int late, int jp2c, int jpx) {
    if (!jpx && jp2c == 0) return "jp2.one-codestream";
    if (!jpx && jp2h != 1) return "jp2.one-jp2h";
    if (jpx && jp2h > 1) return "jpx.one-jp2h";
    if (late) return "jp2h.position";
    return NULL;
}

void hv_header_init(hv_header *h, uint32_t parent, int jpx) {
    memset(h, 0, sizeof *h);
    h->parent = parent;
    h->jpx = jpx;
}

const char *hv_rule_header_child(hv_header *h, uint32_t type) {
    int *count = type == HV_BOX_IHDR ? &h->ihdr : type == HV_BOX_BPCC ? &h->bpcc
               : type == HV_BOX_PCLR ? &h->pclr : type == HV_BOX_CMAP ? &h->cmap
               : type == HV_BOX_CDEF ? &h->cdef : type == HV_BOX_RES ? &h->res : NULL;
    if (h->parent == HV_BOX_JP2H) {
        if (h->children == 0 && type != HV_BOX_IHDR) return "jp2h.ihdr-first";
        if (type == HV_BOX_COLR && h->colr_ended) return "jp2h.colr-contiguous";
        if (type != HV_BOX_COLR && h->colr > 0) h->colr_ended = 1;
    }
    h->children++;
    if (type == HV_BOX_COLR) h->colr++;
    if (count == NULL) return NULL;
    if (++*count > 1) {
        /* T.800 I.5.3: ihdr in other places is ignored; jpch has one. */
        if (type == HV_BOX_IHDR) return h->parent == HV_BOX_JPCH ? "header.one-ihdr" : NULL;
        return type == HV_BOX_BPCC ? "header.one-bpcc" : type == HV_BOX_PCLR ? "header.one-pclr"
             : type == HV_BOX_CMAP ? "header.one-cmap" : type == HV_BOX_CDEF ? "header.one-cdef"
             : "header.one-res";
    }
    if (type == HV_BOX_RES) h->resc = h->resd = 0;
    return NULL;
}

const char *hv_rule_extent(uint32_t type, uint64_t extra) {
    if (extra == 0) return NULL;
    switch (type) {
        case HV_BOX_IHDR: return "ihdr.extent";
        case HV_BOX_CDEF: return "cdef.extent";
        case HV_BOX_RESC: case HV_BOX_RESD: return "res.extent";
        case HV_BOX_RREQ: return "rreq.extent";
        default: return "box.extent";
    }
}

const char *hv_rule_ihdr(hv_header *h, const Ihdr *ihdr) {
    if (h->ihdr == 1) h->image = *ihdr;
    return hv_rule_extent(HV_BOX_IHDR, (uint64_t)ihdr->extra.nCount);
}

const char *hv_rule_bpcc(hv_header *h, const uint8_t *depths, size_t n) {
    if (h->bpcc != 1) return NULL;
    h->bpcc_depths = depths;
    h->bpcc_count = n;
    return NULL;
}

const char *hv_rule_colr(hv_header *h, const ColrHeader *colr, size_t rest) {
    if (colr->meth == 1 && rest != 0) return "colr.enumcs-length";
    if (!h->jpx) {
        /* T.800 I.5.3.3: METH "shall be 1 or 2"; EnumCS 16 (sRGB), 17
         * (greyscale) or 18 (sYCC) in the first colr box. */
        if (colr->meth != 1 && colr->meth != 2) return "colr.method";
        if (h->colr == 1 && colr->meth == 1 &&
            colr->enumcs != 16 && colr->enumcs != 17 && colr->enumcs != 18)
            return "colr.enumcs";
    } else if (h->parent == HV_BOX_JP2H) {
        if ((colr->meth == 1 && h->meth1++ > 0) || (colr->meth == 2 && h->meth2++ > 0))
            return "colr.one-method";
    }
    return NULL;
}

const char *hv_rule_pclr(hv_header *h, uint64_t ne, uint64_t npc) {
    h->pclr_ne = ne;
    h->pclr_bytes = 0;
    if (h->pclr == 1) h->npc = (uint32_t)npc;
    return NULL;
}

const char *hv_rule_pclr_column(hv_header *h, uint64_t depth) {
    h->pclr_bytes += ((depth & 0x7F) + 1 + 7) / 8;
    return NULL;
}

const char *hv_rule_pclr_end(hv_header *h, uint64_t entries) {
    return entries != h->pclr_bytes * h->pclr_ne ? "pclr.entries-length" : NULL;
}

const char *hv_rule_cmap_entry(hv_header *h, const CmapEntry *entry) {
    if (entry->mtyp == 0 && entry->pcol != 0) return "cmap.pcol-zero";
    if (h->cmap != 1) return NULL;
    if (h->cmap_count < UINT32_MAX) h->cmap_count++;
    if (entry->cmp > h->cmap_cmp) h->cmap_cmp = (uint32_t)entry->cmp;
    if (entry->mtyp == 1) {
        h->cmap_palette = 1;
        if (entry->pcol > h->cmap_pcol) h->cmap_pcol = (uint32_t)entry->pcol;
    }
    return NULL;
}

/* Typ is 0, 1, 2 or 65,535 and Asoc at most 65,535 (CdefEntry): a pair
 * where neither is 65,535 has Typ 0 to 2 and Asoc 0 to 65,534, one bit of
 * `seen` each. */
const char *hv_rule_cdef(hv_header *h, const CdefEntry *entries, size_t n) {
    uint8_t seen[3][65535 / 8 + 1];
    const char *error = NULL;
    size_t i;
    memset(seen, 0, sizeof seen);
    for (i = 0; i < n; i++) {
        uint64_t typ = entries[i].typ, asoc = entries[i].asoc;
        if (h->cdef == 1 && entries[i].cn > h->cdef_cn) h->cdef_cn = (uint32_t)entries[i].cn;
        if (typ < 3 && asoc < 65535 && error == NULL) {
            uint8_t *byte = &seen[typ][asoc / 8], bit = (uint8_t)(1u << (asoc % 8));
            if (*byte & bit) error = "cdef.pairs";
            *byte |= bit;
        }
    }
    return error;
}

const char *hv_rule_res_child(hv_header *h, uint32_t type) {
    if ((type == HV_BOX_RESC && h->resc++ > 0) || (type == HV_BOX_RESD && h->resd++ > 0))
        return "res.resc-resd";
    return NULL;
}

const char *hv_rule_res_end(hv_header *h) {
    return h->resc + h->resd == 0 ? "res.resc-resd" : NULL;
}

/* The box of a codestream's header: its own, or else the default. */
static const hv_header *pick(const hv_header *h, const hv_header *d, int present_h, int present_d) {
    return present_h ? h : present_d ? d : NULL;
}

const char *hv_rule_codestream_header(const hv_header *h, const hv_header *d, const hv_siz *siz,
                                      int jpx) {
    static const hv_header none;
    const hv_header *ihdr, *bpcc, *pclr, *cmap;
    const SizFixed *s;
    size_t i;
    int same = 1;
    uint64_t nc, ssiz0 = 0;

    if (h == NULL) h = &none;
    if (d == NULL) d = &none;
    ihdr = pick(h, d, h->ihdr, d->ihdr);
    bpcc = pick(h, d, h->bpcc, d->bpcc);
    pclr = pick(h, d, h->pclr, d->pclr);
    cmap = pick(h, d, h->cmap, d->cmap);
    if (ihdr == NULL) return jpx ? "jpch.ihdr" : "jp2h.ihdr-first";
    nc = ihdr->image.nc;

    if (!jpx) {
        if (h->colr == 0) return "jp2h.colr";
        if ((ihdr->image.bpc == 255) != (bpcc != NULL)) return "ihdr.bpcc";
        if ((pclr != NULL) != (cmap != NULL)) return "header.pclr-cmap";
    } else {
        if (ihdr->image.bpc == 255 && bpcc == NULL) return "ihdr.bpcc";
        if ((pclr != NULL && cmap == NULL) || (cmap != NULL && cmap->cmap_palette && pclr == NULL))
            return "header.pclr-cmap";
    }
    if (bpcc != NULL && ihdr->image.bpc == 255 && bpcc->bpcc_count != nc) return "bpcc.count";
    if (cmap != NULL) {
        if (cmap->cmap_cmp >= nc) return "cmap.component";
        if (cmap->cmap_palette && pclr != NULL && cmap->cmap_pcol >= pclr->npc)
            return "cmap.palette-column";
    }
    if (!jpx && h->cdef && h->cdef_cn >= (cmap != NULL ? cmap->cmap_count : nc))
        return "cdef.channel";

    if (siz == NULL) return NULL;
    s = siz->fixed;
    if (ihdr->image.height != s->ysiz - s->yosiz) return "ihdr.height";
    if (ihdr->image.width != s->xsiz - s->xosiz) return "ihdr.width";
    if (nc != s->csiz) return "ihdr.nc";
    for (i = 0; i < siz->ncomponents; i++) {
        const Component *k = &siz->components[i];
        uint64_t ssiz = k->depthMinus1 | (uint64_t)k->isSigned << 7;
        if (i == 0) ssiz0 = ssiz;
        same &= ssiz == ssiz0;
        if (bpcc != NULL && ihdr->image.bpc == 255 && i < bpcc->bpcc_count &&
            bpcc->bpcc_depths[i] != ssiz)
            return "bpcc.depth";
    }
    if (ihdr->image.bpc != (same ? ssiz0 : 255)) return "ihdr.bpc";
    return NULL;
}
