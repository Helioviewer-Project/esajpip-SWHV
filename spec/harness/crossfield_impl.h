/* crossfield_impl.h — template body for crossfield.c. Included twice with:
 *
 *   CF_S          type-name suffix:  (empty) for layer-1 types, _Profile for layer-2
 *   CF_FILE       the file type:     Jp2Family, Jp2File_Profile or JpxFile_Profile
 *   CF_FN         function name to define
 *   CF_MAIN_OTHER defined when MainBody has the `other` marker alternative
 * Generated-name assumptions (asn1scc C back end):
 *   SEQUENCE OF / OCTET STRING:  .nCount, .arr[]
 *   CHOICE:                      .kind, .u.<alt>, enum <Type>_<alt>_PRESENT
 *   OPTIONAL:                    .exist.<field>
 *   OCTET STRING (CONTAINING X): the field has type X directly
 *   IA5String:                   NUL-terminated char array
 */

#define CF_CAT3_(a, b, c) a##b##c
#define CF_CAT3(a, b, c) CF_CAT3_(a, b, c)
#define CF_T(name) CF_CAT3(name, CF_S, )
#define CF_K(type, alt) CF_CAT3(type, CF_S, _##alt##_PRESENT)

/* Marker codes and box types (decimal, as in the .asn1). */
#define MC_SOC 65359
#define MC_SIZ 65361
#define MC_COD 65362
#define MC_COC 65363
#define MC_QCD 65372
#define MC_TLM 65365
#define MC_PLM 65367
#define MC_PPM 65376
#define MC_POC 65375
#define MC_CRG 65379
#define MC_PLT 65368
#define MC_SOT 65424
#define MC_EOC 65497
#define BT_JP   1783636000UL
#define BT_FTYP 1718909296UL
#define BT_RREQ 1920099697UL
#define BT_JPCH 1785750376UL
#define BT_FTBL 1718903404UL
#define BT_DTBL 1685348972UL
#define BT_JP2C 1785737827UL
#define BT_FLST 1718383476UL
#define BT_URL  1970433056UL

static const char *CF_CAT3(cf_siz, CF_S, )(const CF_T(Siz) *s, cf_layer layer,
                                          const CF_T(Cod) *cod) {
    if (s->csiz != s->components.nCount) return "siz.csiz-count";
    if (!(s->xosiz < s->xsiz && s->yosiz < s->ysiz)) return "siz.origin-inside";
    if (!(s->xtosiz <= s->xosiz && s->ytosiz <= s->yosiz)) return "siz.tile-origin";
    if (!(s->xtosiz + s->xtsiz > s->xosiz && s->ytosiz + s->ytsiz > s->yosiz))
        return "siz.tile-covers-origin";
    if (cod != NULL && cod->sgcod.mct != 0) {
        int i;
        if (s->components.nCount < 3) return "siz.mct-components";
        for (i = 1; i < 3; ++i)
            if (s->components.arr[i].depthMinus1 !=
                        s->components.arr[0].depthMinus1 ||
                s->components.arr[i].xrsiz != s->components.arr[0].xrsiz ||
                s->components.arr[i].yrsiz != s->components.arr[0].yrsiz)
                return "siz.mct-geometry";
    }
    if (layer >= CF_PROFILE) {
        if (!(s->xtsiz >= s->xsiz && s->ytsiz >= s->ysiz)) return "siz.single-tile";
        /* Component sampling 1:1 is a Siz-Profile constraint (WITH COMPONENT). */
    }
    return NULL;
}

static const char *CF_CAT3(cf_cod, CF_S, )(const CF_T(Cod) *c) {
    if (c->spcod.exist.precincts != (c->scod.customPrecincts ? 1 : 0))
        return "cod.precincts-presence";
    if (c->spcod.exist.precincts &&
        c->spcod.precincts.higher.nCount != c->spcod.levels)
        return "cod.precincts-count";
    if (c->spcod.cbWidthExp + c->spcod.cbHeightExp > 8) return "cod.codeblock-area";
    return NULL;
}

/* Value of one Iplt entry (7-bit groups, MSB first). */
static uint64_t CF_CAT3(cf_iplt_value, CF_S, )(const CF_T(Iplt) *e) {
    uint64_t v = e->b0.bits;
#define CF_IPLT_STEP(n) if (e->exist.b##n) v = (v << 7) | (uint64_t) e->b##n.bits
    CF_IPLT_STEP(1); CF_IPLT_STEP(2); CF_IPLT_STEP(3); CF_IPLT_STEP(4);
    CF_IPLT_STEP(5); CF_IPLT_STEP(6); CF_IPLT_STEP(7); CF_IPLT_STEP(8); CF_IPLT_STEP(9);
#undef CF_IPLT_STEP
    return v;
}

/* Total packets = sum over resolutions of precinct counts, times csiz and
 * layers (T.800 B.6, B.7). Returns 0 when the total exceeds INT32_MAX
 * (CodingParameters::FillPrecinctCounts rejects such a codestream). */
static uint64_t CF_CAT3(cf_packet_count, CF_S, )(const CF_T(Siz) *s, const CF_T(Cod) *c) {
    uint64_t total = 0;
    int levels = (int) c->spcod.levels, r;
    for (r = 0; r <= levels; ++r) {
        uint64_t scale = (uint64_t) 1 << (levels - r);
        uint64_t w = ((uint64_t) s->xsiz + scale - 1) / scale;       /* size at r */
        uint64_t h = ((uint64_t) s->ysiz + scale - 1) / scale;
        int ppx = 15, ppy = 15;
        if (c->spcod.exist.precincts) {
            const CF_T(PrecinctSize) *ps = r == 0 ? &c->spcod.precincts.lowest
                                                 : (const CF_T(PrecinctSize) *) &c->spcod.precincts.higher.arr[r - 1];
            ppx = (int) ps->ppx; ppy = (int) ps->ppy;
        }
        w = (w + ((uint64_t) 1 << ppx) - 1) >> ppx;
        h = (h + ((uint64_t) 1 << ppy) - 1) >> ppy;
        total += w * h;
        if (total > 2147483647u) return 0;
    }
    total *= (uint64_t) s->csiz;
    if (total > 2147483647u) return 0;
    total *= (uint64_t) c->sgcod.layers;
    return total > 2147483647u ? 0 : total;
}

static const char *CF_CAT3(cf_codestream, CF_S, )(const CF_T(Codestream) *cs, cf_layer layer) {
    int i, j;
    int cod_before = 0, qcd_before = 0, tile_parts = 0, plts = 0;
    int seen_tile = 0, expect_tpsot = 0, tnsot = 0;
    const CF_T(Cod) *main_cod = NULL;
    const char *r;

    for (i = 0; i < cs->segments.nCount; ++i) {
        const CF_T(MainSegment) *seg = &cs->segments.arr[i];
        int code = (int) seg->code;
        if (seg->body.kind == CF_K(MainBody, tilePart)) {
            const CF_T(TilePart) *tp = &seg->body.u.tilePart;
            if (!seen_tile) {
                if (cod_before != 1) return "codestream.one-cod-before-sot";
                if (qcd_before != 1) return "codestream.one-qcd-before-sot";
                seen_tile = 1;
            }
            tile_parts++;
            if (tp->tpsot != expect_tpsot) return "sot.tpsot-sequence";
            expect_tpsot++;
            if (tp->tnsot != 0) {
                if (tnsot == 0) tnsot = (int) tp->tnsot;
                if (tp->tpsot >= tp->tnsot) return "sot.tpsot-below-tnsot";
                /* A.4.2: TNsot, where given, is the one tile-part count. */
                if ((int) tp->tnsot != tnsot) return "sot.tnsot-inconsistent";
            }
            {
                /* T.800 A.7.3: the PLT segments of a tile-part list the
                 * length of every packet in it, so where any is present
                 * their sum is exactly the tile-part data length.
                 * A.6.1 / A.6.4: COD and QCD appear in a tile-part header at
                 * most once, and only in the first tile-part of the tile. */
                uint64_t plt_sum = 0;
                int expected_zplt = 0;
                int tp_plts = 0;
#ifndef CF_TILE_PLT_ONLY
                int tp_cod = 0, tp_qcd = 0;
#endif
                for (j = 0; j < tp->rest.headers.nCount; ++j) {
                    const CF_T(TileSegment) *ts = &tp->rest.headers.arr[j];
                    if (ts->body.kind == CF_K(TileBody, plt)) {
                        const CF_T(Plt) *plt = &ts->body.u.plt.body;
                        int e;
                        if ((int) plt->zplt != expected_zplt++)
                            return "plt.zplt-sequence";
                        tp_plts++;
                        for (e = 0; e < plt->entries.nCount; ++e) {
                            uint64_t length = CF_CAT3(cf_iplt_value, CF_S, )(&plt->entries.arr[e]);
                            if (layer == CF_STANDARD && length == 0)
                                return "plt.zero-length";
                            plt_sum += length;
                        }
                    }
#ifndef CF_TILE_PLT_ONLY
                    if (ts->body.kind == CF_K(TileBody, cod)) tp_cod++;
                    if (ts->body.kind == CF_K(TileBody, qcd)) tp_qcd++;
#endif
                }
                plts += tp_plts;
                if (tp_plts > 0 && plt_sum != (uint64_t) tp->rest.data.nCount)
                    return "plt.coverage";
#ifndef CF_TILE_PLT_ONLY
                if (tp_cod > 1 || (tp_cod && tp->tpsot != 0)) return "tile.cod-once";
                if (tp_qcd > 1 || (tp_qcd && tp->tpsot != 0)) return "tile.qcd-once";
#endif
                /* Layer 2: TileBody-Profile admits only PLT, so any other
                 * tile-header segment already failed to decode. */
                if (layer >= CF_PROFILE && tp_plts == 0) return "codestream.no-plt";
            }
            continue;
        }
        if (!seen_tile) {
            if (code == MC_COD) { cod_before++; main_cod = &seg->body.u.cod.body; }
            if (code == MC_QCD) qcd_before++;
        } else {
            /* T.800 A.3: after the first SOT only tile-parts and EOC follow. */
            return "codestream.segment-after-sot";
        }
#ifdef CF_MAIN_OTHER
        if (seg->body.kind == CF_K(MainBody, other) &&
            (code == MC_SOC || code == MC_SIZ))
            return "main.other-code";
#endif
    }
    if (tile_parts == 0) return "codestream.no-tile-part";
    if (tnsot != 0 && tile_parts != tnsot) return "sot.tnsot-count";

    if ((r = CF_CAT3(cf_siz, CF_S, )(&cs->siz.body, layer, main_cod)) != NULL) return r;
    for (i = 0; i < cs->segments.nCount; ++i) {
        const CF_T(MainSegment) *seg = &cs->segments.arr[i];
        if (seg->body.kind == CF_K(MainBody, cod) &&
            (r = CF_CAT3(cf_cod, CF_S, )(&seg->body.u.cod.body)) != NULL)
            return r;
#ifndef CF_TILE_PLT_ONLY
        if (seg->body.kind == CF_K(MainBody, tilePart)) {
            const CF_T(TilePart) *tp = &seg->body.u.tilePart;
            for (j = 0; j < tp->rest.headers.nCount; ++j)
                if (tp->rest.headers.arr[j].body.kind == CF_K(TileBody, cod) &&
                    (r = CF_CAT3(cf_cod, CF_S, )(&tp->rest.headers.arr[j].body.u.cod.body)) != NULL)
                    return r;
        }
#endif
    }
    if (layer >= CF_PROFILE) {
        if (tile_parts > 64) return "codestream.tile-part-limit";
        if (plts == 0) return "codestream.no-plt";
        /* FillPrecinctCounts: the packet count must fit the int JPIP state. */
        if (main_cod != NULL &&
            CF_CAT3(cf_packet_count, CF_S, )(&cs->siz.body, main_cod) == 0)
            return "codestream.packet-count";
    }
    return NULL;
}

const char *CF_FN(const CF_FILE *file, cf_layer layer, cf_kind kind) {
    int i, j, k;
    int jp2c = 0, jpch = 0, ftbl = 0, dtbl = 0, rreq = 0, ndr = 0;
    const char *r;

    /* T.800 I.4: signature box then file type box. */
    if (file->boxes.nCount < 2) return "file.two-boxes";
    if (file->boxes.arr[0].tbox != BT_JP ||
        file->boxes.arr[0].payload.kind != CF_K(TopPayload, jP) ||
        file->boxes.arr[0].payload.u.jP.data.nCount != 4 ||
        memcmp(file->boxes.arr[0].payload.u.jP.data.arr, "\x0D\x0A\x87\x0A", 4) != 0)
        return "file.signature";
    if (file->boxes.arr[1].tbox != BT_FTYP) return "file.ftyp-second";
    {
        const Ftyp *ftyp = &file->boxes.arr[1].payload.u.ftyp;
        const char *expected = kind == CF_JP2 ? "jp2 " : "jpx ";
        int compatible = 0;
        if (file->boxes.arr[1].payload.kind != CF_K(TopPayload, ftyp) ||
            memcmp(ftyp->brand.arr, expected, 4) != 0)
            return "file.ftyp-brand";
        for (i = 0; i < ftyp->compat.nCount; ++i)
            compatible |= memcmp(ftyp->compat.arr[i].arr, expected, 4) == 0;
        if (!compatible) return "file.ftyp-compatibility";
    }

    /* First pass: dtbl (ndr is needed to validate fragment references). */
    for (i = 0; i < file->boxes.nCount; ++i) {
        const CF_T(TopBox) *b = &file->boxes.arr[i];
        if (b->payload.kind == CF_K(TopPayload, dtbl)) {
            const CF_T(DataReferences) *d = &b->payload.u.dtbl;
            dtbl++;
            ndr = (int) d->ndr;
            if (d->references.nCount != d->ndr) return "dtbl.ndr-count";
            for (j = 0; j < d->references.nCount; ++j) {
                const CF_T(InnerBox) *u = &d->references.arr[j];
                if (u->payload.kind != CF_K(InnerPayload, url)) return "dtbl.non-url";
                if (layer >= CF_PROFILE && kind == CF_JPX) {
                    const char *loc = (const char *) u->payload.u.url.loc;
                    size_t n = strlen(loc);
                    if (strncmp(loc, "file://", 7) != 0)
                        return "url.file-scheme";   /* ReadUrlBox; ReadJP2 never reads dtbl */
                    if (n < 4 || strcmp(loc + n - 4, ".jp2") != 0)
                        return "url.jp2-target";    /* ReadJPX: links resolve to .jp2 only */
                }
            }
        }
    }

    for (i = 0; i < file->boxes.nCount; ++i) {
        const CF_T(TopBox) *b = &file->boxes.arr[i];
        switch (b->payload.kind) {
            case CF_K(TopPayload, jp2c):
                jp2c++;
                if ((r = CF_CAT3(cf_codestream, CF_S, )(&b->payload.u.jp2c, layer)) != NULL)
                    return r;
                break;
            case CF_K(TopPayload, jpch):
                for (j = 0; j < b->payload.u.jpch.children.nCount; ++j)
                    if (b->payload.u.jpch.children.arr[j].tbox == BT_JP2C)
                        return "jpch.nested-jp2c";
                jpch++;
                break;
            case CF_K(TopPayload, ftbl): {
                const CF_T(Superbox) *sb = &b->payload.u.ftbl;
                int flst = 0;
                ftbl++;
                for (j = 0; j < sb->children.nCount; ++j) {
                    const CF_T(InnerBox) *c = &sb->children.arr[j];
                    if (c->payload.kind != CF_K(InnerPayload, flst)) continue;
                    flst++;
                    for (k = 0; k < c->payload.u.flst.fragments.nCount; ++k) {
                        int dr = (int) c->payload.u.flst.fragments.arr[k].dr;
                        if (dr > ndr) return "flst.dr-range";
                        if (layer >= CF_PROFILE && kind == CF_JPX && dr == 0) return "flst.dr-external";
                    }
                }
                if (flst != 1) return "ftbl.one-flst";     /* T.801 Annex M, Fragment Table box */
                break;
            }
            default:
                break;
        }
    }

    if (kind == CF_JPX) {
        for (i = 0; i < file->boxes.nCount; ++i)
            if (file->boxes.arr[i].tbox == BT_RREQ) rreq++;
        if (layer == CF_STANDARD &&
            (rreq != 1 || file->boxes.nCount < 3 || file->boxes.arr[2].tbox != BT_RREQ))
            return "jpx.reader-requirements";
        /* T.801 M.11.2: "A JPX file shall contain zero or one Data Reference
         * boxes, and that Data Reference box shall be at the top level of the
         * file." */
        if (dtbl > 1) return "jpx.one-dtbl";
        /* T.801 M.11.6: "If Codestream Header boxes appear anywhere in the
         * file, the number of codestreams found in the file shall be the same
         * as the number of available codestream headers." Every jp2c or ftbl
         * is one codestream, numbered by source-box order. A file with no
         * jpch box takes its header information from jp2h and is unconstrained
         * here. The model has no jclx or Multiple Codestream box, so every
         * codestream and every header is top level. */
        if (jpch > 0 && jp2c + ftbl != jpch) return "jpx.codestream-count";
    }

    if (layer >= CF_PROFILE) {
        if (kind == CF_JP2) {
            /* ReadJP2 looks at jp2c boxes only. */
            if (jp2c != 1) return "jp2.one-codestream";
        } else {
            /* ReadJPX requires the header the standard makes optional. */
            if (jpch < 1) return "jpx.no-jpch";
            /* Embedded and linked codestreams do not mix. */
            if (jp2c > 0 && ftbl > 0) return "jpx.mixed-sources";
            if (ftbl > 0 && dtbl != 1) return "jpx.linked-shape";
        }
    }
    return NULL;
}

#undef CF_CAT3_
#undef CF_CAT3
#undef CF_T
#undef CF_K
