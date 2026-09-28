/* crossfield_impl.h — template body for crossfield.c. Included twice with:
 *
 *   CF_S          type-name suffix:  (empty) for layer-1 types, _Profile for layer-2
 *   CF_FILE       the file type:     Jp2Family or JpxFile_Profile
 *   CF_FN         function name to define
 *   CF_COD        the COD body type (optional; default CF_T(Cod))
 *   CF_MAIN_OTHER defined when MainSegment has the `other` marker alternative
 *                 (and so no COC or POC)
 *   CF_TILE_PLT_ONLY defined when TileSegment has the plt alternative only
 * Generated-name assumptions (asn1scc C back end):
 *   SEQUENCE OF / OCTET STRING:  .nCount, .arr[]
 *   CHOICE:                      .kind, .u.<alt>, enum <Type>_<alt>_PRESENT
 *   OPTIONAL:                    .exist.<field>, with the value in .<field>
 *   OCTET STRING (CONTAINING X): the field has type X directly
 *   IA5String:                   NUL-terminated char array
 */

#define CF_CAT3_(a, b, c) a##b##c
#define CF_CAT3(a, b, c) CF_CAT3_(a, b, c)
#define CF_T(name) CF_CAT3(name, CF_S, )
#define CF_K(type, alt) CF_CAT3(type, CF_S, _##alt##_PRESENT)
#ifndef CF_COD
#define CF_COD CF_T(Cod)
#define CF_DEFAULT_COD
#endif


/* The rules on values are shared with the reader: ../../lib/hv_rules.c. */
static const char *CF_CAT3(cf_siz, CF_S, )(const CF_T(Siz) *s, cf_layer layer,
                                          const CF_COD *cod) {
    hv_siz v = cf_siz_view(s);
    return hv_rule_siz(&v, cod != NULL ? &cod->sgcod : NULL, layer >= CF_PROFILE);
}

static const char *CF_CAT3(cf_cod, CF_S, )(const CF_COD *c, cf_layer layer) {
    return hv_rule_cod(&c->scod, &c->spcod, layer >= CF_PROFILE);
}

/* The bytes an Iplt takes: b0 and the bytes after it. */
static size_t CF_CAT3(cf_iplt_size, CF_S, )(const Iplt *e) {
    return 1u + e->exist.b1 + e->exist.b2 + e->exist.b3 + e->exist.b4 + e->exist.b5 +
           e->exist.b6 + e->exist.b7 + e->exist.b8 + e->exist.b9;
}

/* Psot: the tile-part's bytes from SOT to the end of its data (A.4.2), as
 * the encoding lays them out: SOT, each header segment's code, Lxxx and
 * body, SOD, the data. */
static uint64_t CF_CAT3(cf_psot, CF_S, )(const CF_T(TilePart) *tp) {
    uint64_t n = 12 + 2 + (uint64_t) tp->rest.data.nCount;
    int j, e;
    for (j = 0; j < tp->rest.headers.nCount; ++j) {
        const CF_T(TileSegment) *ts = &tp->rest.headers.arr[j];
        n += 4;
        if (ts->exist.plt) {
            n += 1;
            for (e = 0; e < ts->plt.body.entries.nCount; ++e)
                n += CF_CAT3(cf_iplt_size, CF_S, )(&ts->plt.body.entries.arr[e]);
        }
#ifndef CF_TILE_PLT_ONLY
        if (ts->exist.cod)
            n += 10 + (uint64_t) ts->cod.body.spcod.precincts.nCount;
        if (ts->exist.qcd) n += 1 + (uint64_t) ts->qcd.body.spqcd.nCount;
        if (ts->exist.com) n += 2 + (uint64_t) ts->com.body.ccom.nCount;
        if (ts->exist.coc) n += (uint64_t) ts->coc.body.data.nCount;
        if (ts->exist.qcc) n += (uint64_t) ts->qcc.body.data.nCount;
        if (ts->exist.rgn) n += (uint64_t) ts->rgn.body.data.nCount;
        if (ts->exist.poc) n += (uint64_t) ts->poc.body.data.nCount;
        if (ts->exist.ppt) n += (uint64_t) ts->ppt.body.data.nCount;
        if (ts->exist.other) n += (uint64_t) ts->other.body.data.nCount;
        if (ts->exist.noSegment) n -= 2;    /* a code without Lxxx */
#endif
    }
    return n;
}

static const char *CF_CAT3(cf_codestream, CF_S, )(const CF_T(Codestream) *cs, cf_layer layer) {
    int i, j;
    int cod_before = 0, qcd_before = 0, tile_parts = 0, plts = 0;
    int seen_tile = 0;
    int all_parts_have_plt = 1, packet_layout_override = 0;
    int profile = layer >= CF_PROFILE;
    hv_plt_count count = { 0, 0 };
    hv_segments segments;
    uint32_t tiles, unfinished = 0;
    const CF_COD *main_cod = NULL;
    const hv_siz siz = cf_siz_view(&cs->siz.body);
    const char *r;

    /* In the reader's order (../../lib/hv_reader.c): SIZ, then each
     * segment as it comes, a COD's rules where the COD is. */
    if ((r = CF_CAT3(cf_siz, CF_S, )(&cs->siz.body, layer, NULL)) != NULL) return r;
    tiles = hv_rule_tiles(&siz);
    memset(cf_tile_counts, 0, tiles * sizeof *cf_tile_counts);
    memset(cf_components, 0, siz.ncomponents * sizeof *cf_components);
    hv_segments_init(&segments, &siz, profile ? NULL : cf_components, profile);

    for (i = 0; i < cs->segments.nCount; ++i) {
        const CF_T(MainSegment) *seg = &cs->segments.arr[i];
        int code = (int) seg->code;
        if (seg->exist.tilePart) {
            const CF_T(TilePart) *tp = &seg->tilePart;
            hv_zplt zplt;
            if (!seen_tile) {
                if (cod_before != 1) return "codestream.one-cod-before-sot";
                if (qcd_before != 1) return "codestream.one-qcd-before-sot";
                seen_tile = 1;
            }
            tile_parts++;
            /* A.4.2, per tile: Isot in the grid, TPsot in order, TNsot. */
            if ((r = hv_rule_isot(tiles, tp->isot)) != NULL ||
                (r = hv_rule_tile_part(&cf_tile_counts[tp->isot], &unfinished, tp->tpsot,
                                       tp->tnsot)) != NULL ||
                (r = hv_segments_tile_part(&segments, &cf_tile_counts[tp->isot], tp->isot,
                                           tp->tpsot, CF_CAT3(cf_psot, CF_S, )(tp))) != NULL)
                return r;
            memset(&zplt, 0, sizeof zplt);
            {
                /* T.800 A.7.3: the PLT segments of a tile-part list the
                 * length of every packet in it (hv_segments_data).
                 * A.6.1 / A.6.4: COD and QCD appear in a tile-part header at
                 * most once, and only in the first tile-part of the tile. */
                uint64_t plt_sum = 0, zeros = 0;
                int tp_plts = 0;
#ifndef CF_TILE_PLT_ONLY
                int tp_cod = 0, tp_qcd = 0;
#endif
                for (j = 0; j < tp->rest.headers.nCount; ++j) {
                    const CF_T(TileSegment) *ts = &tp->rest.headers.arr[j];
#ifndef CF_TILE_PLT_ONLY
                    const Opaque *body = ts->exist.coc ? &ts->coc.body
                                       : ts->exist.qcc ? &ts->qcc.body
                                       : ts->exist.rgn ? &ts->rgn.body
                                       : ts->exist.poc ? &ts->poc.body
                                       : ts->exist.ppt ? &ts->ppt.body : NULL;
                    if (ts->exist.cod || ts->exist.coc || ts->exist.poc || ts->exist.ppt)
                        packet_layout_override = 1;
                    if (ts->exist.cod) {
                        if (++tp_cod > 1 || tp->tpsot != 0) return "tile.cod-once";
                        if ((r = CF_CAT3(cf_cod, CF_S, )(&ts->cod.body, layer)) != NULL)
                            return r;
                        /* A tile-part COD's MCT needs the same components. */
                        if ((r = hv_rule_siz(&siz, &ts->cod.body.sgcod, profile)) != NULL)
                            return r;
                    }
                    if (ts->exist.qcd && (++tp_qcd > 1 || tp->tpsot != 0))
                        return "tile.qcd-once";
                    if ((ts->exist.cod || ts->exist.qcd || body != NULL) &&
                        (r = hv_segments_tile(&segments, (uint16_t) ts->code,
                                              ts->exist.cod ? &ts->cod.body : NULL,
                                              ts->exist.qcd ? &ts->qcd.body : NULL,
                                              body ? body->data.arr : NULL,
                                              body ? (size_t) body->data.nCount : 0)) != NULL)
                        return r;
#endif
                    if (ts->exist.plt) {
                        const Plt *plt = &ts->plt.body;
                        int e;
                        if ((r = hv_rule_zplt(&zplt, plt->zplt, profile)) != NULL) return r;
                        tp_plts++;
                        for (e = 0; e < plt->entries.nCount; ++e) {
                            uint64_t length;
                            if ((r = hv_rule_iplt(&plt->entries.arr[e], &length)) != NULL)
                                return r;
                            if (length > (uint64_t) tp->rest.data.nCount - plt_sum)
                                return "plt.coverage";
                            if ((r = hv_rule_plt_entry(&count, length, profile)) != NULL)
                                return r;
                            plt_sum += length;
                            zeros += length == 0;
                        }
                    }
                }
                plts += tp_plts;
                if (tp_plts == 0) all_parts_have_plt = 0;
                /* Layer 2: TileSegment-Profile admits only PLT, so any other
                 * tile-header segment already failed to decode. */
                if (profile && tp_plts == 0) return "codestream.no-plt";
                if ((r = hv_segments_data(&segments, tp->rest.data.arr,
                                          (size_t) tp->rest.data.nCount, (unsigned) tp_plts,
                                          plt_sum, zeros, profile ? NULL : &zplt)) != NULL)
                    return r;
            }
            continue;
        }
        /* Every segment has its field: a code with none is outside the
         * MainMarkerCode value set and failed to decode (the reader's
         * main.marker-code; ../check-model.sh checks the value sets). */
#ifndef CF_MAIN_OTHER
        if (seg->exist.coc || seg->exist.poc || seg->exist.ppm) packet_layout_override = 1;
#endif
        if (!seen_tile) {
#ifndef CF_MAIN_OTHER
            const Opaque *body = seg->exist.coc ? &seg->coc.body
                               : seg->exist.qcc ? &seg->qcc.body
                               : seg->exist.rgn ? &seg->rgn.body
                               : seg->exist.poc ? &seg->poc.body
                               : seg->exist.tlm ? &seg->tlm.body
                               : seg->exist.plm ? &seg->plm.body
                               : seg->exist.ppm ? &seg->ppm.body
                               : seg->exist.crg ? &seg->crg.body : NULL;
#endif
            if (code == HV_COD) {
                if (++cod_before > 1) return "codestream.one-cod-before-sot";
                main_cod = &seg->cod.body;
                if ((r = CF_CAT3(cf_cod, CF_S, )(main_cod, layer)) != NULL) return r;
                if ((r = CF_CAT3(cf_siz, CF_S, )(&cs->siz.body, layer, main_cod)) != NULL)
                    return r;
            }
            if (code == HV_QCD && ++qcd_before > 1) return "codestream.one-qcd-before-sot";
#ifndef CF_MAIN_OTHER
            if ((code == HV_COD || code == HV_QCD || body != NULL) &&
                (r = hv_segments_main(&segments, (uint16_t) code,
                                      code == HV_COD ? &seg->cod.body : NULL,
                                      code == HV_QCD ? &seg->qcd.body : NULL,
                                      body ? body->data.arr : NULL,
                                      body ? (size_t) body->data.nCount : 0)) != NULL)
                return r;
#endif
        } else {
            /* T.800 A.3: after the first SOT only tile-parts and EOC follow. */
            return "codestream.segment-after-sot";
        }
    }
    if (tile_parts == 0) return "codestream.no-tile-part";
    if ((r = hv_rule_tile_parts_end(unfinished)) != NULL) return r;
    if ((r = hv_segments_end(&segments)) != NULL) return r;

    if (profile) {
        /* TilePart-Profile's TPsot and TNsot reject a 65th tile-part
         * first; this is the reader's name for the same limit. */
        if ((r = hv_rule_tile_part_count((uint64_t) tile_parts)) != NULL) return r;
    }
    if (layer == CF_STANDARD) {
        const CF_T(Siz) *s = &cs->siz.body;
        const SizFixed *f = &s->fixed;
        if (!all_parts_have_plt || packet_layout_override ||
            f->xosiz != 0 || f->yosiz != 0 ||
            f->xtosiz != 0 || f->ytosiz != 0 ||
            f->xtsiz < f->xsiz || f->ytsiz < f->ysiz)
            return NULL;
        for (i = 0; i < s->components.nCount; ++i)
            if (s->components.arr[i].xrsiz != 1 ||
                s->components.arr[i].yrsiz != 1)
                return NULL;
    }
    if (main_cod != NULL && plts > 0)
        return hv_rule_plt_packets(&count, &siz, &main_cod->sgcod,
                                   &main_cod->spcod, profile);
    return NULL;
}

/* The box type of an inner box, as far as the rules of hv_rule_child
 * distinguish them (0: any other). */
static uint32_t CF_CAT3(cf_inner_type, CF_S, _)(const CF_T(InnerBox) *b) {
    switch (b->payload.kind) {
        case CF_K(InnerPayload, flst): return HV_BOX_FLST;
        case CF_K(InnerPayload, url):  return HV_BOX_URL;
        case CF_K(InnerPayload, jp2c): return HV_BOX_JP2C;
        case CF_K(InnerPayload, jpch): return HV_BOX_JPCH;
        case CF_K(InnerPayload, ftbl): return HV_BOX_FTBL;
        case CF_K(InnerPayload, dtbl): return HV_BOX_DTBL;
        default:                       return 0;
    }
}

/* A Data Entry URL box of a dtbl: nothing after LOC's NUL. Layer 1 names
 * that box.extent (hv_rule_extent); layer 2 passes LOC, its NUL and the
 * bytes after it to hv_rule_url (ReadUrlBox; ReadJPX: links resolve to
 * .jp2 only), which names it url.terminator, as the reader does. */
static const char *CF_CAT3(cf_url, CF_S, )(const CF_T(DataEntryUrl) *url, cf_layer layer) {
    uint8_t raw[sizeof url->loc + sizeof url->extra.arr];
    size_t n = strlen((const char *) url->loc) + 1, extra = (size_t) url->extra.nCount;
    if (layer < CF_PROFILE) return hv_rule_extent(HV_BOX_URL, (uint64_t) extra);
    memcpy(raw, url->loc, n);
    memcpy(raw + n, url->extra.arr, extra);
    return hv_rule_url(url->header.vers, url->header.flag, raw, n + extra);
}

const char *CF_FN(const CF_FILE *file, cf_layer layer, cf_kind kind) {
    int i, j, k;
    int ndr = 0;
    hv_jpx_boxes count = { 0, 0, 0, 0, 0, 0, 0 };
    const char *r;

    /* T.800 I.4: signature box then file type box. */
    /* The reader's order: the signature, then a second box. */
    if (file->boxes.arr[0].payload.kind != CF_K(TopPayload, jP) ||
        !cf_signature(&file->boxes.arr[0].payload.u.jP))
        return "file.signature";
    if (file->boxes.nCount < 2) return "file.two-boxes";
    if (file->boxes.arr[1].payload.kind != CF_K(TopPayload, ftyp))
        return "file.ftyp-second";
    if ((r = cf_ftyp(&file->boxes.arr[1].payload.u.ftyp, kind, layer)) != NULL) return r;

    /* First pass: dtbl (ndr is needed to validate fragment references). */
    for (i = 0; i < file->boxes.nCount; ++i) {
        const CF_T(TopBox) *b = &file->boxes.arr[i];
        if (b->payload.kind == CF_K(TopPayload, dtbl)) {
            const CF_T(DataReferences) *d = &b->payload.u.dtbl;
            count.dtbl++;
            ndr = (int) d->ndr;
            if (d->references.nCount != (int) d->ndr) return "dtbl.ndr-count";
            for (j = 0; j < d->references.nCount; ++j) {
                const CF_T(InnerBox) *u = &d->references.arr[j];
                if ((r = hv_rule_child(HV_BOX_DTBL, CF_CAT3(cf_inner_type, CF_S, _)(u))) != NULL)
                    return r;
                if ((r = CF_CAT3(cf_url, CF_S, )(&u->payload.u.url, layer)) != NULL)
                    return r;
            }
        }
    }

    for (i = 0; i < file->boxes.nCount; ++i) {
        const CF_T(TopBox) *b = &file->boxes.arr[i];
        switch (b->payload.kind) {
            case CF_K(TopPayload, jp2c):
                count.jp2c++;
                break;
            case CF_K(TopPayload, jpch):
                for (j = 0; j < b->payload.u.jpch.children.nCount; ++j)
                    if ((r = hv_rule_child(HV_BOX_JPCH, CF_CAT3(cf_inner_type, CF_S, _)(
                                               &b->payload.u.jpch.children.arr[j]))) != NULL)
                        return r;
                count.jpch++;
                break;
            case CF_K(TopPayload, ftbl): {
                const CF_T(Superbox) *sb = &b->payload.u.ftbl;
                int flst = 0;
                count.ftbl++;
                for (j = 0; j < sb->children.nCount; ++j) {
                    const CF_T(InnerBox) *c = &sb->children.arr[j];
                    if ((r = hv_rule_child(HV_BOX_FTBL, CF_CAT3(cf_inner_type, CF_S, _)(c))) != NULL)
                        return r;
                    if (c->payload.kind != CF_K(InnerPayload, flst)) continue;
                    flst++;
                    /* T.801 M.11.3.1: NF fragments. */
                    if ((int) c->payload.u.flst.nf != c->payload.u.flst.fragments.nCount)
                        return "flst.nf-count";
                    if (layer >= CF_PROFILE &&
                        (r = hv_rule_flst(c->payload.u.flst.nf,
                                          c->payload.u.flst.fragments.nCount)) != NULL)
                        return r;
                }
                if (flst != 1) return "ftbl.one-flst";     /* T.801 Annex M, Fragment Table box */
                break;
            }
            default:
                break;
        }
    }

    /* The count rules (hv_rule_jpx). The model has no jclx or Multiple
     * Codestream box (they are `other`): cf_extensions says whether the
     * file has one, whose codestreams and headers the count leaves out.
     * Layer 2 for a .jp2 is cf_check_jp2_profile, in crossfield.c. */
    if (kind == CF_JPX) {
        for (i = 0; i < file->boxes.nCount; ++i)
            if (file->boxes.arr[i].payload.kind == CF_K(TopPayload, rreq)) count.rreq++;
        count.rreq_third = file->boxes.nCount >= 3 &&
                           file->boxes.arr[2].payload.kind == CF_K(TopPayload, rreq);
        count.extensions = layer < CF_PROFILE && cf_extensions;
        if ((r = hv_rule_jpx(&count, layer >= CF_PROFILE)) != NULL) return r;
    }

    /* The data references, after the count rules, as the reader checks
     * them: a linked file without a dtbl is jpx.linked-shape, not a DR out
     * of range. */
    for (i = 0; i < file->boxes.nCount; ++i) {
        const CF_T(TopBox) *b = &file->boxes.arr[i];
        if (b->payload.kind != CF_K(TopPayload, ftbl)) continue;
        for (j = 0; j < b->payload.u.ftbl.children.nCount; ++j) {
            const CF_T(InnerBox) *c = &b->payload.u.ftbl.children.arr[j];
            if (c->payload.kind != CF_K(InnerPayload, flst)) continue;
            for (k = 0; k < c->payload.u.flst.fragments.nCount; ++k)
                if ((r = hv_rule_fragment_dr(c->payload.u.flst.fragments.arr[k].dr,
                                             (uint64_t) ndr, layer >= CF_PROFILE)) != NULL)
                    return r;
        }
    }

    /* The codestreams, after the file's boxes, as the reader reads them
     * (hv_check_served: the file rules, then each codestream). */
    for (i = 0; i < file->boxes.nCount; ++i) {
        const CF_T(TopBox) *b = &file->boxes.arr[i];
        if (b->payload.kind == CF_K(TopPayload, jp2c) &&
            (r = CF_CAT3(cf_codestream, CF_S, )(&b->payload.u.jp2c, layer)) != NULL)
            return r;
    }
    return NULL;
}

#undef CF_CAT3_
#undef CF_CAT3
#undef CF_T
#undef CF_K
#ifdef CF_DEFAULT_COD
#undef CF_COD
#undef CF_DEFAULT_COD
#endif
