/* hv_writer.h: writes JPEG 2000 headers with the encoders generated from
 * spec/jpeg2000-io.asn1, into a buffer that grows as needed.
 *
 * Marker segment lengths (Lxxx) come from the generated encoders. The
 * lengths that cover data the generated code never sees, Psot and LBox or
 * XLBox, are filled in when the tile-part or box ends. */
#ifndef HV_WRITER_H
#define HV_WRITER_H

#include <stddef.h>
#include <stdint.h>

#include "jpeg2000-io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *data;
    size_t size, capacity;
    const char *error;      /* the first error; later writes do nothing */
} hv_out;

void hv_out_init(hv_out *out);
void hv_out_free(hv_out *out);

/* Back to out->size `size`, taken while out->error was NULL: drops what was
 * written since and the error of those writes. The only way to shorten
 * out; callers do not set out->size. */
void hv_out_rewind(hv_out *out, size_t size);

/* Every write returns 0, or -1 with out->error set. */

/* Bytes the writer does not interpret: packet data, box payloads. */
int hv_write_bytes(hv_out *out, const void *bytes, size_t size);

/* A marker without a segment: SOC, SOD, EOC. */
int hv_write_marker(hv_out *out, uint16_t code);

/* Any marker segment from its code and body; Lxxx is added. */
int hv_write_segment(hv_out *out, uint16_t code, const uint8_t *body, size_t size);

/* SIZ, COD, QCD and COM from their decoded form, code included. */
int hv_write_siz(hv_out *out, const SizSegment_Std *siz);
int hv_write_cod(hv_out *out, const CodSegment_Std *cod);
int hv_write_qcd(hv_out *out, const QcdSegment_Std *qcd);
int hv_write_com(hv_out *out, const ComSegment_Std *com);

/* PLT segments listing `count` packet lengths, Zplt from 0. Each segment
 * holds as many whole entries as fit in Lplt = 65 535, which is how Kakadu
 * splits them. Zero lengths are rejected (T.800 A.7.3). */
int hv_write_plt(hv_out *out, const uint64_t *lengths, size_t count);

/* SOT with Psot left to hv_end_tile_part, which sets it to the bytes
 * written since *start. */
int hv_begin_tile_part(hv_out *out, uint16_t isot, uint8_t tpsot, uint8_t tnsot,
                       size_t *start);
int hv_end_tile_part(hv_out *out, size_t start);

/* A box header whose length hv_end_box fills in. Begun `extended`, the
 * box has LBox = 1 and XLBox; otherwise LBox, which hv_end_box switches to
 * XLBox if the box outgrew it (above 4 GiB - 1), moving the payload up 8
 * bytes: an offset taken inside the box is then 8 short. */
int hv_begin_box(hv_out *out, uint32_t type, int extended, size_t *start);
int hv_end_box(hv_out *out, size_t start);

/* A box header for a payload of `size` bytes the caller writes itself:
 * LBox, or LBox = 1 and XLBox above 4 GiB - 9 bytes. */
int hv_write_box_header(hv_out *out, uint32_t type, uint64_t size);

/* JPX boxes (T.801 Annex M), with the served profile's types. */

/* A Fragment List box with one fragment (FragmentList-Profile). */
int hv_write_flst(hv_out *out, uint64_t offset, uint32_t length, uint16_t dr);

/* NDR, the start of a Data Reference box's contents. */
int hv_write_ndr(hv_out *out, uint16_t ndr);

/* A Data Entry URL box, version 0 and flags 0, with LOC and its NUL. */
int hv_write_url(hv_out *out, const char *loc);

/* A Reader Requirements box (T.801 M.11.1, Rreq-Std): ML from the masks,
 * which must all have its length (1, 2, 4 or 8 bytes). */
int hv_write_rreq(hv_out *out, const Rreq_Std *rreq);

#ifdef __cplusplus
}
#endif

#endif
