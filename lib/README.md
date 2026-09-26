# lib

The JPEG 2000 reader/writer, built by the repository's CMake as the static
library `jpeg2000_io`. `hv_transcode` (in `../transcode/`) uses it; the
server may later. The header types come from the ASN.1/ACN model in
`../spec/`; the code here steps from header to header and records where each
payload starts and ends.

## Layout

| File | Role |
| --- | --- |
| `hv_reader.h` / `.c` | Steps through a file in memory: boxes (T.800 I.4) and codestream items (Annex A). Checks framing and decodes marker segments with the generated code. Never copies a payload. |
| `hv_rules.h` / `.c` | The model's cross-field rules on marker segment bodies, for both layers: SIZ, COD, the PLT entries and the packet count. The corpus harness (`../spec/harness/crossfield_impl.h`) uses the same code, so the reader applies the rules the corpus labels come from. |
| `hv_geometry.h` / `.c` | The resolutions, bands, precincts and code-blocks of one tile, and its packets in progression order (T.800 B.2 to B.7, B.12), from the decoded SIZ and COD. Counts and derives the partition instead of storing it; numbers code-blocks so that two precinct partitions with the same code-block partition agree. No COC or POC. |
| `hv_writer.h` / `.c` | Writes box headers, SIZ, COD, QCD, COM, PLT, SOT and opaque segments with the generated encoders into a growing buffer. Fills in Psot and LBox/XLBox when a tile-part or box ends (switching a box to XLBox if it outgrows LBox), and splits PLT the way Kakadu does (as many whole entries as fit in Lplt = 65 535). |
| `hv_mapping.c` | The one ACN mapping function the generated code calls (`lxxx`: Lxxx counts itself). |
| `hv_walk.c` | `hv_walk [-v] [-p] [-P] [-w] file...`: checks files with the reader and prints one line per file; `-v` lists every box and codestream item, `-p` accepts trailing zero PLT entries, `-P` checks the served profile, `-w` rewrites the whole file with the writer from what the reader decoded and compares it with the input. |
| `test/` | `test_profile`: every JP2 vector of `../tests/vectors/j2k` must pass `hv_check_jp2` and `HV_PROFILE` exactly when the manifest labels it profile-valid. Run with the transcoder's tests (`../transcode/test/run.sh`). |
| `generated/` | Code generated from `../spec/` by `generate.sh`: the types in `../spec/jpeg2000-io.asn1`, the profile types the reader checks against, and what they use. |
| `generate.sh` | Regenerates `generated/` with the pinned asn1scc (Docker image, or `ASN1SCC=...`). |
| `CMakeLists.txt` | The `jpeg2000_io` library and the `hv_walk` tool, added by the top-level `CMakeLists.txt`; the tests with `ESAJPIP_TRANSCODE_TESTS`. |

## Build

With the rest of the repository:

```sh
cmake -S . -B build [-DESAJPIP_SANITIZE=ON]
cmake --build build --target hv_walk
```

After a model change in `../spec/`, run `lib/generate.sh`. It needs the
compiler that `../spec/build-asn1scc.sh` builds: the pinned revision with the
local patches in `../spec/asn1scc-patches/`. The unpatched compiler generates
a PLT decoder that reads an uninitialized flag on truncated input.

## What the reader checks

- Boxes: LBox, and XLBox when LBox = 1, within the container; LBox = 0 only
  for the last box, and inside a superbox only if the superbox also runs to
  the end of the file.
- Codestream: SOC then SIZ; exactly one COD and one QCD in the main header;
  marker placement per Table A.1; tile-part order, TPsot and TNsot; Psot, or
  up to the final EOC when Psot = 0; nothing after EOC.
- Bodies, by the generated decoders: SIZ, COD, QCD, PLT and COM at the
  standard's bounds, plus the model's cross-field rules (`hv_rules.c`):
  SIZ, COD, zero packet lengths, and PLT sums against the tile-part data.
- `HV_ACCEPT_PLT_PADDING` (`hv_walk -p`) accepts zero PLT entries after the
  last packet of each tile-part. T.800 does not allow them, but deployed
  files carry them: every one of 4,014 EUI files checked has them.
- The served profile (`JPIP_PROFILE.md`, the model's layer 2), in two
  scopes. `HV_PROFILE_HEADERS`, the main header: SIZ as `Siz-Profile`
  (zero origins, unit sampling, dimensions up to `INT32_MAX`) and one tile;
  no COC, POC or PPM, no marker T.800 places elsewhere and none of 0xFF30
  to 0xFF3F (`MainMarkerCode-Profile`). This is what a transcoder
  needs of its input, since it rewrites the tile-parts. `HV_PROFILE`, all
  of it: also no SOP; tile-part headers with PLT only
  (`TileMarkerCode-Profile`) and at least one; at most 64 tile-parts; one
  nonzero PLT entry per packet of the main COD, with zero entries only after
  the last packet of the codestream; a packet count up to `INT32_MAX`.
  Errors from shared rules are the rule names of the corpus manifest
  (`siz.single-tile`).
- `hv_check_jp2` (`hv_walk -P`), the profile's file rules for a JP2 file:
  at most `INT_MAX` bytes, the signature box, then the file type box with
  the `jp2 ` brand and compatibility entry, top-level boxes framed as above,
  exactly one `jp2c`. A raw codestream fails.

Not yet: JPX rules, box bodies other than the box header and `ftyp`, and,
outside the profile, PLT against the packet count. The model's standard
layer counts packets where the layout allows (one tile, no COC or POC),
which the reader does only with `HV_PROFILE`. Unknown marker codes are
reported as items and skipped by length; the caller decides.
