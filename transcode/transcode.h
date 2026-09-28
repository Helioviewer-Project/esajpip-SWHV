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
 * order, no COC, POC or PPM, no tile-part header markers other than PLT
 * and COM, and code-block styles without selective arithmetic coding bypass
 * or termination on each coding pass. SOP and EPH markers are checked
 * (A.8) and not written; the PLT is checked (as the reader does) but not
 * used. Of the main header, QCD, QCC, RGN, CRG and COM are kept, TLM and
 * PLM dropped, and any other marker rejected: 0xFF30 to 0xFF3F and codes
 * T.800 does not define too. Of the tile-part headers, PLT and COM are
 * dropped with the headers, and any other marker rejected. main_marker and
 * tile_marker in transcode.c decide this, for both functions below. */
#ifndef HV_TRANSCODE_H
#define HV_TRANSCODE_H

#include <stddef.h>
#include <stdint.h>

#include "hv_reader.h"
#include "hv_writer.h"

/* Transcodes the codestream in buf[start, end) and appends the result to
 * out. Precinct width and height exponents: 1 to 15 (2 to 32,768 samples).
 * flags: hv_codestream_open flags the input must also pass, 0 or
 * HV_PROFILE_HEADERS; any other value fails. The input is read with
 * HV_ACCEPT_PLT_PADDING, zero PLT entries accepted at the end of each
 * tile-part, since its PLT is not used; hv_codestream_open refuses that
 * flag with HV_PROFILE, whose PLT rules apply to the output. 0, or -1 with
 * a message in error; on failure out is as it was (hv_out_rewind), and
 * fails at once if out->error is set. */
int hv_transcode_codestream(const uint8_t *buf, size_t start, size_t end, int ppx, int ppy,
                            unsigned flags, hv_out *out, char *error, size_t error_size);

/* Transcodes the JP2 file in buf[0, size) for the JPIP server and appends
 * the result to out. The input must be one the output can serve: it must
 * pass the served profile's file rules (hv_check_jp2: no JPX, no raw
 * codestream) and main-header rules (HV_PROFILE_HEADERS), and have the
 * header boxes T.800 requires (hv_check_jp2h), which it keeps. What else the
 * profile asks for is written here: the tile-parts, and a COD without SOP.
 * The output passes HV_PROFILE and is at most INT_MAX bytes. The boxes are
 * kept in order: the codestream box is transcoded, a top-level XML box ends
 * before its first NUL (XML has none; old Kakadu wrote one at the end),
 * every other box is copied as read and superboxes are checked.
 * 0, or -1 with a message in error; on failure out is as it was
 * (hv_out_rewind), and fails at once if out->error is set. */
int hv_transcode_file(const uint8_t *buf, size_t size, int ppx, int ppy, hv_out *out,
                      char *error, size_t error_size);

#endif
