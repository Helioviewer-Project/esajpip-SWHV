/* label.h — labels a file at both layers of the model: decodes it with the
 * generated decoders of layer 1 (Jp2Family) and layer 2 (Jp2File-Profile
 * or JpxFile-Profile), which also check the ASN.1 constraints, and applies
 * crossfield.c. vectors.c labels the corpus with it, writers.c the files
 * that ../../jpeg2000's writer writes. */
#ifndef J2K_HARNESS_LABEL_H
#define J2K_HARNESS_LABEL_H

#include <stddef.h>
#include <stdint.h>

#include "crossfield.h"

/* The layers, as far as their box trees differ: layer 1 of a JPX file
 * (Jp2Family) and of a JP2 file (Jp2File), layer 2 of a .jpx
 * (JpxFile-Profile) and layer 2 of a .jp2 (Jp2File-Profile). */
typedef enum {
    LAYER_STANDARD, LAYER_STANDARD_JP2, LAYER_PROFILE_JPX, LAYER_PROFILE_JP2
} BoxLayer;

/* Where a layer reads a box's payload as boxes: the offset of the first
 * child in the payload (a dtbl's children follow NDR), or -1 where the layer
 * keeps the box opaque; `depth` is 0 for a top-level box. This is the one
 * list of superboxes in the harness, and it follows the model's types:
 * TopPayload's Superbox, Association and DataReferences alternatives and
 * InnerPayload's Res and Cgrp at layer 1 of a JPX file, Jp2Payload's
 * Jp2Header and Jp2HeaderPayload's Res at layer 1 of a JP2 file,
 * TopPayload-Profile's Superbox-Profile and DataReferences-Profile at
 * layer 2 of a .jpx, none at layer 2 of a .jp2 (Jp2Payload-Profile). The
 * box tree rules (hv_rule_box_tree, ../../jpeg2000/hv_rules.c) read deeper, to
 * every superbox of the file's kind, and so do hv_is_superbox's callers:
 * hv_walk and hv_transcode check the framing of every box that holds
 * boxes, at any depth. */
int children_offset(uint32_t type, int depth, BoxLayer layer);

/* A file's labels: valid at each layer, or the first check that failed
 * there, a rule name or "decode". Layer 1 reads a file whose ftyp brand is
 * 'jpx ' as a JPX file, any other as a JP2 file; layer 2 by `kind`, the
 * file's extension, as the server does. */
typedef struct {
    int std_ok, prof_ok;
    const char *std_reason, *prof_reason;
} Label;

/* Allocates the decode targets; call once before label. */
void label_init(void);

/* A companion that a linked JPX file may name: its URL (kept, not copied)
 * and the extent of its codestream, measured from the file, not from JPX
 * claims. A corpus oracle, not a filesystem resolver; at most 16. */
void label_companion(const char *url, uint64_t offset, uint32_t length);
void label_companions_clear(void);

Label label(const unsigned char *data, size_t len, cf_kind kind);

#endif
