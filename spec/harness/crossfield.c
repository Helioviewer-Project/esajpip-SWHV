/* crossfield.c — instantiates crossfield_impl.h for both struct families. */
#include <stdint.h>
#include <string.h>
#include "crossfield.h"

/* Layer-1 family: boxes have `other`; codestream markers do not. */
#define CF_S
#define CF_FILE Jp2Family
#define CF_FN cf_check_family
#include "crossfield_impl.h"
#undef CF_S
#undef CF_FILE
#undef CF_FN

/* Layer-2 family: *_Profile types, whose codestream has the `other` marker
 * alternative and whose box tree shares the standard layer's `other` boxes.
 * Jp2File_Profile and JpxFile_Profile share every nested type, so one
 * instantiation serves both; the two public entry points fix the kind. */
#define CF_S _Profile
#define CF_FILE Jp2File_Profile
#define CF_FN cf_check_profile_common_
#define CF_MAIN_OTHER
#define CF_TILE_PLT_ONLY     /* TileBody-Profile has the plt alternative only */
#include "crossfield_impl.h"
#undef CF_S
#undef CF_FILE
#undef CF_FN
#undef CF_MAIN_OTHER
#undef CF_TILE_PLT_ONLY

const char *cf_check_jp2_profile(const Jp2File_Profile *file) {
    return cf_check_profile_common_(file, CF_PROFILE, CF_JP2);
}

const char *cf_check_jpx_profile(const JpxFile_Profile *file) {
    /* JpxFile_Profile and Jp2File_Profile are structurally identical
     * (both are SEQUENCE { boxes SEQUENCE OF TopBox-Profile }); the cast is
     * the one place that relies on it. If the generator emits different
     * layouts, instantiate the template a third time instead. */
    return cf_check_profile_common_((const Jp2File_Profile *) file, CF_PROFILE, CF_JPX);
}
