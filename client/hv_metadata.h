/* hv_metadata.h: what the metadata data-bins of a store say about the target.
 *
 * Metadata bin 0 is the file's boxes, with a placeholder box (phld, T.808
 * A.3.6.3) in place of each codestream and, in a JPX file, of each top-level
 * association box, whose contents are a metadata bin of their own. The
 * server sends them all with the first response of a channel. */
#ifndef HV_METADATA_H
#define HV_METADATA_H

#include <stddef.h>
#include <stdint.h>

#include "hv_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hv_metadata_frame hv_metadata_frame;
typedef struct {
    size_t count;
    hv_metadata_frame *frames;
} hv_metadata;

/* Indexes complete metadata bins once. Start with {0}; close before reopening.
 * Returns 0, or -1 with a message and an empty index. The index borrows the
 * complete bins' bytes, which stay fixed until hv_cache_release; adding other
 * bins does not invalidate it. Close the index before releasing the store. */
int hv_metadata_open(const hv_cache *cache, hv_metadata *metadata,
                     char *error, size_t error_size);
void hv_metadata_close(hv_metadata *metadata);

/* The XML that describes a codestream, as hv_merge and hvJP2K write it: in a
 * JPX file, the contents of the xml box of the first top-level association
 * box whose number list names the codestream or the compositing layer of
 * that number; failing that, of the first top-level xml box, which a JP2
 * file has for its one codestream. 0, with *xml in the store (good until
 * hv_cache_release) and *size its length, or *xml NULL when the file
 * has none; -1, with a message in error, if the frame index is out of range. */
int hv_metadata_xml(const hv_metadata *metadata, uint64_t codestream, const uint8_t **xml, size_t *size,
                    char *error, size_t error_size);

/* The color table of a codestream: the palette (pclr, T.800 I.5.3.4) that
 * its component mapping (cmap, I.5.3.5) applies to a component, from the
 * file's JP2 Header box or, in a JPX file, box by box from the
 * codestream's own header box (jpch) where that has one (T.801 M.11.6).
 * A decoded sample of the component is then an index into the table.
 *
 * Returns the number of entries (1 to 1,024), 0 when the codestream has no
 * color table, or -1 with a message in error for an invalid index or boxes that
 * are not a palette. *channels gets the values of an entry: the channels
 * the mapping makes of the component, in its order (3 for red, green,
 * blue). table, when capacity holds entries * *channels bytes, gets them
 * entry by entry, each value scaled to 8 bits; at most 1,024 * 255 bytes. */
enum { HV_PALETTE_MAX = 1024 * 255 };
int hv_metadata_palette(const hv_metadata *metadata, uint64_t codestream, int *channels, uint8_t *table,
                        size_t capacity, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
