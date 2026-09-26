/* hv_rules.c: see hv_rules.h. */
#include "hv_rules.h"

#include <stddef.h>

const char *hv_rule_siz(const Siz *s, const Sgcod *sgcod, int profile) {
    int i;
    if ((int)s->csiz != s->components.nCount) return "siz.csiz-count";
    if (!(s->xosiz < s->xsiz && s->yosiz < s->ysiz)) return "siz.origin-inside";
    if (!(s->xtosiz <= s->xosiz && s->ytosiz <= s->yosiz)) return "siz.tile-origin";
    if (!(s->xtosiz + s->xtsiz > s->xosiz && s->ytosiz + s->ytsiz > s->yosiz))
        return "siz.tile-covers-origin";
    if (sgcod != NULL && sgcod->mct != 0) {
        const Component *k = s->components.arr;
        if (s->components.nCount < 3) return "siz.mct-components";
        for (i = 1; i < 3; i++)
            if (k[i].depthMinus1 != k[0].depthMinus1 || k[i].xrsiz != k[0].xrsiz ||
                k[i].yrsiz != k[0].yrsiz)
                return "siz.mct-geometry";
    }
    if (profile) {
        if (s->xosiz != 0 || s->yosiz != 0 || s->xtosiz != 0 || s->ytosiz != 0)
            return "siz.zero-origin";
        for (i = 0; i < s->components.nCount; i++)
            if (s->components.arr[i].xrsiz != 1 || s->components.arr[i].yrsiz != 1)
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

uint64_t hv_rule_packets(const Siz *s, const Sgcod *sgcod, const Spcod *spcod) {
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

const char *hv_rule_plt_packets(const hv_plt_count *count, const Siz *siz,
                                const Sgcod *sgcod, const Spcod *spcod, int profile) {
    uint64_t packets = hv_rule_packets(siz, sgcod, spcod);
    if (packets == 0)
        return profile ? "codestream.packet-count" : NULL;
    if (profile && count->padding && count->packets < packets) return "plt.zero-length";
    if (count->packets != packets) return "plt.packet-count";
    return NULL;
}
