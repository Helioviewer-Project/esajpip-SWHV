/* transcode.h: rewrites a JPEG 2000 codestream in RPCL order with the given
 * precincts and PLT markers, without recompressing it, as
 *   kdu_transcode Corder=RPCL ORGgen_plt=yes Cprecincts={W,H}
 * does. A port of hvJP2K's jp2_precincts.transcode_codestream.
 *
 * Every code-block keeps its coding passes, bytes and zero bit-planes; only
 * Tier-2 changes. That is possible while the new precincts leave the
 * code-block partition unchanged.
 *
 * Supported input: one tile (any number of tile-parts), any progression
 * order, no COC, POC, PPM, RGN or tile-part header markers other than PLT
 * and COM, and code-block styles without selective arithmetic coding bypass
 * or termination on each coding pass. SOP and EPH markers are skipped and
 * not written; TLM and PLM are dropped; COM is kept. */
#ifndef HV_TRANSCODE_H
#define HV_TRANSCODE_H

#include <stddef.h>
#include <stdint.h>

#include "hv_writer.h"

/* Precinct width and height exponents: 1 to 15 (2 to 32 768 samples). */
int hv_transcode_codestream(const uint8_t *buf, size_t start, size_t end, int ppx, int ppy,
                            hv_out *out, char *error, size_t error_size);

#endif
