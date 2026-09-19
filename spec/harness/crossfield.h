/* crossfield.h — the cross-field rules listed at the end of each .asn1,
 * implemented once as a template (crossfield_impl.h) and instantiated for
 * both struct families the generator emits: the layer-1 types
 * (Jp2Family, TopBox, Codestream, ...) and the layer-2 types
 * (Jp2File_Profile, TopBox_Profile, Codestream_Profile, ...).
 *
 * Each checker returns NULL when every rule of the requested layer holds,
 * otherwise a short rule name (the same names the manifest uses). The two
 * layers share their common structural rules, while the profile adds server
 * restrictions and explicitly omits documented standard rules that esajpip
 * does not enforce.
 */
#ifndef J2K_HARNESS_CROSSFIELD_H
#define J2K_HARNESS_CROSSFIELD_H

#include "jp2-boxes.h"       /* generated: adjust to the emitted header names */
#include "j2k-codestream.h"
#include "j2k-headers.h"

typedef enum { CF_STANDARD = 1, CF_PROFILE = 2 } cf_layer;
typedef enum { CF_JP2 = 1, CF_JPX = 2 } cf_kind;

/* Layer-1 struct family (file decoded as Jp2Family). */
const char *cf_check_family(const Jp2Family *file, cf_layer layer, cf_kind kind);

/* Layer-2 struct families. */
const char *cf_check_jp2_profile(const Jp2File_Profile *file);
const char *cf_check_jpx_profile(const JpxFile_Profile *file);

#endif
