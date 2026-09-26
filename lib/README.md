# lib

The JPEG 2000 reader/writer, built by the repository's CMake as the static
library `jpeg2000_io`. `hv_transcode` (in `../transcode/`) and `hv_merge`
(in `../merge/`) use it; the server may later. The header types come from
the ASN.1/ACN model in `../spec/`; the code here steps from header to
header and records where each payload starts and ends.

## Layout

| File | Role |
| --- | --- |
| `hv_reader.h` / `.c` | Steps through a file in memory: boxes (T.800 I.4) and codestream items (Annex A). Checks framing and decodes marker segments with the generated code, which also gives the bytes a variable-size field takes (`DECODE_USED`), one element at a time (see below). Never copies a payload. A codestream item carries the decoded segment: SIZ as `hv_siz` (`SizFixed` and its `Component`s), COD and QCD as `CodSegment_Std` and `QcdSegment_Std`, PLT as `hv_plt` (Zplt and where its entries are, read with `hv_plt_next`), COM as `hv_com` (Rcom and its text, in place). |
| `hv_codes.h` | The marker codes, box types and brands the code refers to, the box header sizes, `HV_LARGEST(T)`, the size of a generated type's largest encoding (its `_REQUIRED_BYTES_FOR_ACN_ENCODING`), and `HV_FIXED(T)`, the same for a fixed-size type: used instead of hand-counted layout sizes. |
| `hv_rules.h` / `.c` | The model's cross-field rules, for both layers: on marker segment bodies (SIZ, COD, the tile-parts, the PLT entries and the packet count), the JPX boxes, and the JP2 header boxes (standard layer), and the profile's limit of 64 tile-parts. The corpus harness (`../spec/harness/crossfield_impl.h`, `crossfield.c`) uses the same code, so the reader applies the rules the corpus labels come from. A check returns a rule name or NULL, nothing else, and does not allocate; `hv_rule_tiles` and `hv_rule_packets` are the two counts, not checks. |
| `hv_geometry.h` / `.c` | The resolutions, bands, precincts and code-blocks of one tile, and its packets in progression order (T.800 B.2 to B.7, B.12), from the decoded SIZ and COD. Counts and derives the partition instead of storing it; numbers code-blocks so that two precinct partitions with the same code-block partition agree. No COC or POC; at most 2,147,483,647 packets. |
| `hv_writer.h` / `.c` | Writes box headers, SIZ, COD, QCD, COM, PLT, SOT and opaque segments, the JPX boxes the server reads (`flst`, `url`, and a `dtbl`'s NDR), and the Reader Requirements box, with the generated encoders into a growing buffer, one element at a time. Measures every length it fills in: Lxxx when a SIZ, PLT, COM or opaque segment ends, Psot when a tile-part ends, LBox/XLBox when a box ends (switching a box to XLBox if it outgrows LBox). Splits PLT the way Kakadu does (as many whole entries as fit in Lplt = 65,535), once per tile-part. Each encoder runs with room for its type's largest encoding, at most 197 bytes (`QcdSegment_Std`), because the generated encoders do not check the room. The buffer's spare capacity is kept zero, because the generated encoders skip zero bits (`BitStream_AppendNBitZero` only moves on): an append clears nothing, and `hv_out_rewind` zeroes what it drops. An `Iplt` is encoded after a guard byte: the generated `Iplt_ACN_Encode` writes a 0 bit at the start of its stream for each absent byte's `more` determinant, which would clear the `more` bit of an entry of 2 to 8 bytes. |
| `hv_error.h` / `.c` | `hv_fail`: formats an error message into the caller's buffer and returns -1, for `hv_geometry` and the tools. |
| `hv_served.h` / `.c` | The served profile's checks of a whole file on disk (`hv_check_served`), the files a JPX file links to included, and the file reader they use: shared by `hv_walk -P` and `test_profile`. |
| `hv_mapping.c` | The one ACN mapping function the generated code calls (`lxxx`: Lxxx counts itself). |
| `hv_walk.c` | `hv_walk [-v] [-p] [-P] [-H] [-w] file...`: checks files with the reader and prints one line per file: `valid`, `invalid` with the error and its offset, `differs` when `-w` rewrote a valid file differently, `ERROR` when it cannot be read. `-v` lists every box and codestream item, `-p` accepts trailing zero PLT entries (`-p` with `-P` is a usage error), `-P` checks the served profile (`hv_check_served`: a `.jpx` with its linked files, whose codestreams it counts as linked), `-H` the header boxes, `-w` rewrites the whole file with the writer from what the reader decoded and compares it with the input. A file is JPX when its name ends in `.jpx` in any case (the server picks the format by the extension), and JP2 otherwise; without `-P` and `-H` a file that starts with SOC is read as a raw codestream, while `-P` and `-H` fail it with `file.signature`. |
| `test/` | `test_profile`: every vector of `../tests/vectors/j2k` must pass the reader's checks of the served profile (`hv_check_served`) exactly when the manifest labels it profile-valid. The header box checks (`hv_check_jp2h`, `hv_check_jpx_headers`) are held to a contract taken from the manifest's labels and their own answers, not from vector names: they accept every standard-valid vector; a vector they reject is standard-invalid by the rule they name (any error for reason `decode`); a rule they name for one vector they name for every vector with that reason; and they reject every standard-invalid, profile-valid vector except the two profile leniencies they do not read, which the test lists (PLT padding, `asoc` contents). Also unit checks the corpus cannot reach (URL decoding, `cdef` pairs, accessors and items, `hv_plt_next` on entries of ten bytes, truncated, of eleven bytes or above 64 bits, error offsets). `test_writer`: `hv_writer.c` compiled with `HV_LBOX_MAX` 100, for the XLBox switch, the zeroed spare capacity, the measured Lxxx (SIZ, COM, the PLT split at Lplt 65,535), PLT entries of 1 to 10 bytes, the `flst`, `url` and `rreq` boxes byte for byte, and the error messages, read back with the reader. `run.sh` builds and runs them with the tools' tests (`ESAJPIP_TOOL_TESTS`, label `tools`). |
| `generated/` | Code generated from `../spec/` by `generate.sh`: the types its `pdus` list names (those of `../spec/jpeg2000-io.asn1`, the served-profile types the reader checks against, `Fragment`, and the JP2 header box types) and what they use. A type the code here needs that none of them uses must be added to `pdus`. |
| `generate.sh` | Regenerates `generated/` with the pinned asn1scc (Docker image, or `ASN1SCC=...`); `--check` compares without replacing it (`../spec/check-model.sh` runs it so). |
| `CMakeLists.txt` | The `jpeg2000_io` library and the `hv_walk` tool, added by the top-level `CMakeLists.txt`; the tests with `ESAJPIP_TOOL_TESTS`. |

## Build

With the rest of the repository, so configuring needs the server's
dependencies too: the top-level `CMakeLists.txt` requires zlib, glib,
llhttp and libuv before it adds `lib/`, `transcode/` and `merge/`.

```sh
cmake -S . -B build [-DESAJPIP_SANITIZE=ON]
cmake --build build --target hv_walk
```

Tests, with the tools' and separate from the server's:
`lib/test/run.sh [normal|sanitize] [CTest options]`; without a mode, the
tests run in normal mode and CTest options may come first
(`lib/test/run.sh -R merge`). The build goes to
`build/tool-tests-<mode>`, or to `ESAJPIP_TEST_BUILD_DIR`, which
`../tests/run.sh` also reads: set it for one runner at a time.

After a model change in `../spec/`, run `lib/generate.sh`. It needs the
compiler that `../spec/build-asn1scc.sh` builds: the pinned revision with
every local patch that `../spec/asn1scc-patches/series` lists, in order
(`../spec/asn1scc-patches/README.md` describes each). Without
`0004-icdpdus-reference-init`, for one, what `generate.sh` makes with
`-icdPdus` does not link, and without `0001-deferred-determinant-uninit`
the PLT decoder reads an uninitialized flag on truncated input.

## One element at a time

No code here holds a list as a generated struct at the standard's bound.
The types of `../spec/jpeg2000-io.asn1` are small: a fixed part or one
element of a list, or a whole COD or QCD segment, which are small enough
(at most 45 and 197 bytes) to decode in one piece. The reader decodes a
list element by element up to the end its segment or box length gives:

- SIZ: `SizFixed`, then `Component`s to the end of the segment, as many as
  a Csiz can count (else `invalid SIZ`), into an array allocated for them;
  `siz.csiz-count` compares Csiz with the number decoded. In the profile,
  after the named rules, `SizFixed-Profile` and `Component-Profile`, the
  two parts of `Siz-Profile`.
- PLT: `Zplt`, then `Iplt` entries to the end of the segment, all decoded
  (`invalid PLT`) before the rules: `plt.zplt-sequence`, then each entry
  (`plt.value-overflow`, the padding rules), then `plt.coverage` at SOD.
  The entries stay in the caller's buffer; `hv_plt_next` reads them.
- COM: `Rcom`, then at least one byte of text, which stays in place.
- `rreq`: `RreqHeader` (ML, FUAM, DCM, NSF), NSF `RreqStandardFeature`s,
  NVF (`FeatureCount`), NVF `RreqVendorFeature`s, each feature handed
  exactly its code and ML bytes, and `rreq.extent` for bytes after them.
- `flst`: NF (`FragmentCount`), then the `Fragment`s, one at a time.

The writer encodes the same elements one at a time and never takes a
length from a type's size: it measures what it wrote (a segment's Lxxx,
Psot, LBox). `HV_FIXED(T)` appears only for fixed-size types (a marker
code, Lxxx, SOT, `Component`, `Fragment`, a feature's code); wherever a
measured length works, the reader uses `DECODE_USED` instead.

`hv_siz` is SIZ as the rules and the geometry take it, however it was
decoded: the reader points it at its `SizFixed` and component array, the
corpus harness at the whole-file model's `Siz`
(`{&siz.fixed, siz.components.arr, nCount}`).

## Struct sizes

asn1scc 4.9.3.0, C, 64-bit: the C struct and the largest encoding of each
type the reader and writer decode or encode.

| Type | struct (bytes) | largest encoding (bytes) |
| --- | ---: | ---: |
| `BoxHeader` | 32 | 16 |
| `FtypHeader` | 16 | 8 |
| `SotSegment` | 40 | 10 |
| `SizFixed` | 80 | 36 |
| `Component` | 32 | 3 |
| `CodSegment_Std` | 616 | 45 |
| `QcdSegment_Std` | 208 | 197 |
| `Zplt` | 8 | 1 |
| `Iplt` | 88 | 10 |
| `Rcom` | 8 | 2 |
| `UrlHeader` | 16 | 4 |
| `RreqHeader` | 32 | 19 |
| `RreqStandardFeature` | 24 | 10 |
| `RreqVendorFeature` | 28 | 24 |
| `Fragment` | 24 | 14 |
| `Ihdr` | 128 | 78 |
| `ColrHeader` | 40 | 7 |
| `PclrCounts` | 16 | 3 |
| `CmapEntry` | 24 | 4 |
| `CdefEntry` | 24 | 6 |
| `Resolution` | 120 | 74 |

`MarkerCode`, `SegmentLength`, `DataReferenceCount`, `FeatureCount`,
`FragmentCount`, `BitDepth` and `CdefCount` are 8-byte integers.
`hv_codestream` is 2,000 bytes; it allocates only SIZ's components (32 bytes
each, at most 16,384: more is `invalid SIZ`) and the per-tile tile-part
counts. `hv_header`, for the header
box checks, is 256 bytes: of a `bpcc` box it keeps where the entries are
(`hv_rule_bpcc`), which the file's buffer holds until the codestream's
header is checked.

## What the reader checks

- Boxes: LBox, and XLBox when LBox = 1, within the container; LBox = 0 only
  for the last box, and inside a superbox only if the superbox also runs to
  the end of the file.
- Codestream: SOC then SIZ; exactly one COD and one QCD in the main header;
  marker placement per Table A.1; tile-part order, TPsot and TNsot; Psot, or
  up to the final EOC when Psot = 0; nothing after EOC. A tile-part header
  that reaches SOT or EOC is "tile-part header without SOD" with any flags.
  The accessors give the main header's SIZ, COD and QCD bodies once read,
  and NULL before, for a segment the reader rejected, and after
  `hv_codestream_close`.
- Bodies, by the generated decoders: COD and QCD whole, SIZ, PLT and COM
  one element at a time (above), plus the model's cross-field rules
  (`hv_rules.c`): SIZ, COD, zero packet lengths, and PLT sums against the
  tile-part data.
- `HV_ACCEPT_PLT_PADDING` (`hv_walk -p`) accepts zero PLT entries after the
  last packet of each tile-part. T.800 does not allow them, but deployed
  files carry them: every one of 4,014 EUI files checked has them. A
  nonzero entry after a zero one fails as `plt.padding-position`, the
  profile's rule, applied to the tile-part instead of the codestream. Not
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
  Errors name the rule that fails (`siz.single-tile`,
  `codestream.one-cod-before-sot`), with the names the corpus manifest uses
  where a vector exercises the rule; a main-header or tile-part-header
  marker out of place is `main.marker-code` or `tile.marker-code`.
  Failures of the decoders themselves (`invalid SIZ`, `truncated SIZ`) and
  against a profile type (`SIZ: Xsiz or Ysiz above 2,147,483,647
  (Siz-Profile)`) are lowercase prose. Every error comes with the offset of
  the box, marker segment or entry at fault, or, for a rule on the file's
  box counts (`file.two-boxes`, `jp2.one-codestream`, `jpx.*`,
  `jp2h.position`), the end of the file; a function that succeeds leaves
  the offset alone.
- `hv_check_jp2` (`hv_walk -P`), the profile's file rules for a JP2 file:
  at most `INT_MAX` bytes, the signature box, then the file type box with
  the `jp2 ` brand and compatibility entry, top-level boxes framed as above,
  exactly one `jp2c`. A raw codestream fails.
- `hv_check_jpx` (`hv_walk -P` on a `.jpx`), the profile's file rules for
  a JPX file, as `ReadJPX` reads it: the signature and a file type box with
  the `jpx ` brand; the children of `jpch`, `ftbl` and `dtbl`, placed as
  T.801 Annex M requires (`hv_rule_child`); the count rules
  (`hv_rule_jpx`); one fragment per `ftbl` (a fragment with LEN 0 fails as
  such, not as `flst.one-fragment`), and version-0 `file://` URLs naming
  `.jp2` files, whose percent-escapes decode (`url.percent-encoding`). It
  returns the codestreams, embedded or linked; `hv_codestream_check` reads
  an embedded one, `hv_link_path` resolves a link as the server does (or
  says why it cannot), and `hv_check_link` checks the linked file and that
  the fragment is exactly its codestream. `flst.dr-range` and
  `flst.dr-external` point at the `flst` box. Other boxes, `asoc`
  included, are opaque, as they are to the server. `hv_check_served`
  (`hv_walk -P`) does all of it for a file on disk.
- `hv_check_jp2h` and `hv_check_jpx_headers` (`hv_walk -H`), the header
  boxes, at the standard layer (the server reads none of them, but a
  client decodes the image by them): where `jp2h` is; the placement of
  the children of every top-level superbox (`hv_rule_child`, as the
  harness checks at layer 1: no `jp2c`, `jpch`, `ftbl` or `dtbl` below the
  top level, `flst` only in `ftbl`, `url` only in `dtbl`, except that a
  `url` in `jp2h`, `jplh`, `uinf` or `asoc` is not checked), and one
  `flst` per `ftbl`, in a JP2 file too;
  the children of `jp2h`, `jpch` and `jplh`, decoded one box or entry at a
  time with the model's types (`Ihdr`, `BitDepth`, `ColrHeader`,
  `PclrCounts`, `CmapEntry`, `CdefCount`, `CdefEntry`, `Resolution`) and
  checked by the header rules of `hv_rules.c`; and each codestream's
  header (for a JPX file its `jpch` over the `jp2h` defaults) against its
  SIZ, where the codestream is embedded. A rule comparing boxes points at the
  codestream's header box (its `jpch`, else `jp2h`); a codestream whose
  SIZ does not open fails with the reader's error at its offset. For a JPX
  file also the count rules of `hv_rule_jpx` at the standard layer (the
  Reader Requirements box, one and the third; at most one `dtbl`; a
  codestream per `jpch`), at most one `jp2h` (`jpx.one-jp2h`, T.801
  M.11.5), and the Reader Requirements box's contents, decoded one part
  at a time (above). The top-level boxes are read a fixed number of
  times, whatever the number of codestreams. `hv_transcode` and
  `hv_merge` require `hv_check_jp2h` of their inputs.
- Bytes after the fields of `ihdr`, `resc`, `resd`, `cdef` and `rreq` are
  `<box>.extent` however many there are: the reader counts those of
  `cdef` and `rreq`, and decodes the other types from at most their
  largest encoding.
  The model's `Extra` holds 64 bytes (a corpus bound), so the harness
  labels a box with more `decode`.

Not yet: box bodies other than the box header, `ftyp`, `flst`, `url`, a
`dtbl`'s NDR, `rreq` and the header boxes, and, outside the profile, PLT
against the packet count. The model's standard layer counts packets where
the layout allows (one tile, no COC or POC), which the reader does only
with `HV_PROFILE`. Unknown marker codes are reported as items and skipped,
by their length, or, from 0xFF30 to 0xFF3F, which have no marker segment,
by the marker alone; the caller decides.
