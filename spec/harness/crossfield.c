/* crossfield.c — instantiates crossfield_impl.h for both struct families,
 * and checks a .jp2 at layer 2, whose box types differ (Jp2File_Profile). */
#include <stdint.h>
#include <string.h>
#include "crossfield.h"

/* Tile-part counts per tile, for hv_rule_tile_part: Isot addresses at most
 * 65 535 tiles. */
static uint16_t cf_tile_parts[65535];
static uint8_t cf_tile_tnsot[65535];

/* T.800 I.5.1: the signature box's contents. */
static int cf_signature(const OpaqueBox *box) {
    return box->data.nCount == 4 && memcmp(box->data.arr, "\x0D\x0A\x87\x0A", 4) == 0;
}

/* T.800 I.5.2 / T.801 M.8: the brand for the file's kind, and in the
 * compatibility list. */
static const char *cf_ftyp(const Ftyp *ftyp, cf_kind kind) {
    const char *expected = kind == CF_JP2 ? "jp2 " : "jpx ";
    int i, compatible = 0;
    if (memcmp(ftyp->brand.arr, expected, 4) != 0) return "file.ftyp-brand";
    for (i = 0; i < ftyp->compat.nCount; ++i)
        compatible |= memcmp(ftyp->compat.arr[i].arr, expected, 4) == 0;
    return compatible ? NULL : "file.ftyp-compatibility";
}

/* Layer-1 family: boxes have `other`; codestream markers do not. */
#define CF_S
#define CF_FILE Jp2Family
#define CF_FN cf_check_family
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
