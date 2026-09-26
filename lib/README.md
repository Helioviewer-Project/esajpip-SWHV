# lib

The JPEG 2000 reader/writer, built by the repository's CMake as the static
library `jpeg2000_io`. `hv_transcode` (in `../transcode/`) and
`hv_merge` (in `../merge/`) use it; the
server may later. The header types come from the ASN.1/ACN model in
`../spec/`; the code here steps from header to header and records where each
payload starts and ends.

## Layout

| File | Role |
| --- | --- |
| `hv_reader.h` / `.c` | Steps through a file in memory: boxes (T.800 I.4) and codestream items (Annex A). Checks framing and decodes marker segments with the generated code, which also gives the bytes a variable-size field takes (`DECODE_USED`). Never copies a payload. |
| `hv_codes.h` | The marker codes, box types and brands the code refers to, the box header sizes, and `HV_FIXED(T)`: the encoded size of a fixed-size generated type (its `_REQUIRED_BYTES_FOR_ACN_ENCODING`), used instead of hand-counted layout sizes. |
| `hv_rules.h` / `.c` | The model's cross-field rules, for both layers: on marker segment bodies (SIZ, COD, the tile-parts, the PLT entries and the packet count), the JPX boxes, and the JP2 header boxes (standard layer), and the profile's limit of 64 tile-parts. The corpus harness (`../spec/harness/crossfield_impl.h`, `crossfield.c`) uses the same code, so the reader applies the rules the corpus labels come from. |
| `hv_geometry.h` / `.c` | The resolutions, bands, precincts and code-blocks of one tile, and its packets in progression order (T.800 B.2 to B.7, B.12), from the decoded SIZ and COD. Counts and derives the partition instead of storing it; numbers code-blocks so that two precinct partitions with the same code-block partition agree. No COC or POC. |
| `hv_writer.h` / `.c` | Writes box headers, SIZ, COD, QCD, COM, PLT, SOT and opaque segments, the JPX boxes the server reads (`flst`, `url`, and a `dtbl`'s NDR), and the Reader Requirements box (`Rreq-Std`), with the generated encoders into a growing buffer. Fills in Psot and LBox/XLBox when a tile-part or box ends (switching a box to XLBox if it outgrows LBox), and splits PLT the way Kakadu does (as many whole entries as fit in Lplt = 65 535). |
| `hv_mapping.c` | The one ACN mapping function the generated code calls (`lxxx`: Lxxx counts itself). |
| `hv_walk.c` | `hv_walk [-v] [-p] [-P] [-H] [-w] file...`: checks files with the reader and prints one line per file; `-v` lists every box and codestream item, `-p` accepts trailing zero PLT entries (not with `-P`), `-P` checks the served profile (a `.jpx` with its linked files), `-H` the header boxes, `-w` rewrites the whole file with the writer from what the reader decoded and compares it with the input. |
| `test/` | `test_profile`: every vector of `../tests/vectors/j2k` must pass the reader's profile mode (`hv_check_jp2` or `hv_check_jpx`, then `HV_PROFILE` for every codestream, embedded or linked) exactly when the manifest labels it profile-valid; the header box checks must accept every standard-valid vector and reject, by name, each one a header rule makes standard-invalid. `run.sh` builds and runs it with the tools' tests (`ESAJPIP_TOOL_TESTS`, label `tools`). |
| `generated/` | Code generated from `../spec/` by `generate.sh`: the types in `../spec/jpeg2000-io.asn1`, the profile types the reader checks against, and what they use. |
| `generate.sh` | Regenerates `generated/` with the pinned asn1scc (Docker image, or `ASN1SCC=...`); `--check` compares without replacing it. |
| `CMakeLists.txt` | The `jpeg2000_io` library and the `hv_walk` tool, added by the top-level `CMakeLists.txt`; the tests with `ESAJPIP_TOOL_TESTS`. |

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
  files carry them: every one of 4,014 EUI files checked has them. Not
  with `HV_PROFILE`, which accepts them after the codestream's last packet
  only: `hv_codestream_open` refuses the pair.
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
- `hv_check_jpx` (`hv_walk -P` on a `.jpx`), the profile's file rules for
  a JPX file, as `ReadJPX` reads it: the signature and a file type box with
  the `jpx ` brand; the children of `jpch`, `ftbl` and `dtbl`, placed as
  T.801 Annex M requires (`hv_rule_child`); the count rules
  (`hv_rule_jpx`); one fragment per `ftbl`, and version-0 `file://` URLs
  naming `.jp2` files. It returns the codestreams, embedded or linked;
  `hv_codestream_check` reads an embedded one, `hv_link_path` resolves a
  link as the server does, and `hv_check_link` checks the linked file and
  that the fragment is exactly its codestream. Other boxes, `asoc`
  included, are opaque, as they are to the server.
- `hv_check_jp2h` and `hv_check_jpx_headers` (`hv_walk -H`), the header
  boxes, at the standard layer (the server reads none of them, but a
  client decodes the image by them): where `jp2h` is; the children of
  `jp2h`, `jpch` and `jplh`, decoded one box or entry at a time with the
  model's types (`Ihdr`, `BitDepth`, `ColrHeader`, `PclrHeader`,
  `CmapEntry`, `CdefCount`, `CdefEntry`, `Resolution`) and checked by the
  header rules of `hv_rules.c`; and each codestream's header (for a JPX
  file its `jpch` over the `jp2h` defaults) against its SIZ, where the
  codestream is embedded. For a JPX file also the Reader Requirements box:
  one, the third box, its contents decoded with `Rreq-Std` (the model's
  `Rreq` at the standard's bounds, about 3.4 MB, allocated per check). `hv_transcode` and `hv_merge` require
  `hv_check_jp2h` of their inputs.

Not yet: box bodies other than the box header, `ftyp`, `flst`, `url` and
the header boxes, and,
outside the profile, PLT against the packet count. The model's standard
layer counts packets where the layout allows (one tile, no COC or POC),
which the reader does only with `HV_PROFILE`. Unknown marker codes are
reported as items and skipped by length; the caller decides.
