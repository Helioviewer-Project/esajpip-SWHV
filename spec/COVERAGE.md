# JPEG 2000 served-profile coverage

This map connects the rules in the ASN.1/ACN model to generated vectors and
the esajpip source that enforces the served profile. It answers a narrower
question than a JPEG 2000 decoder conformance suite: does esajpip accept the
file structures it documents and reject malformed or unsupported structures?

The authoritative list of individual vectors and labels is
`tests/vectors/j2k/manifest.tsv`. A label is written as `standard/profile`.
For example, `valid/invalid` usually means that the structure is permitted by
the cited JPEG 2000 standard but excluded from the esajpip served profile.
For linked-source extent and availability tests, the standard label covers
the JPX structure only; the profile oracle additionally checks the companion
file. A structurally valid fragment list need not reference a valid codestream.
Vector names below are representative. The manifest-driven test runs all of them.

## Coverage map

| Structure and reference | Model and rule | Accepted evidence | Rejected evidence | Server enforcement and profile decision |
| --- | --- | --- | --- | --- |
| Box lengths and containment, T.800 Annex I and T.801 Annex M | `TopBox`, `InnerBox`, ACN `lbox`, and `size deduced` regions in `jp2-boxes.*` | The four base files and all unchanged structural mutants | `*-len-*` vectors for top-level boxes and interpreted superboxes | `ReadBoxHeader` bounds each box against its enclosing container. Metadata boxes that esajpip skips as a whole, including `jp2h`, `jplh`, and `uinf`, are opaque in the profile model, and so are the contents of the children of `jpch`. The inner-length mutants exercise `jp2h`, `jpch`, and in the linked base `ftbl` and `dtbl`. Those inside `jp2h` are standard-invalid but profile-accepted; those inside `jpch`, `ftbl` and `dtbl` are invalid at both layers, because the server walks their children. |
| JP2 signature, T.800 I.4 and I.5.1 | `file.signature` in `crossfield_impl.h` | `jp2.jp2` | `jp2-sig-bad.jp2`, `jp2-rule-file.signature-37.jp2` | `ReadImage` checks the complete 12-byte signature before dispatching by extension. |
| File Type box, T.800 I.5.2 and T.801 Annex M | `Ftyp`, `file.ftyp-brand`, and `file.ftyp-compatibility` | `jp2-rule-file.ftyp-compatibility-36.jp2` | `jp2-rule-file.ftyp-brand-34.jp2`, `jp2-rule-file.ftyp-compatibility-35.jp2` | `ReadFileType` requires the extension-specific brand and a matching compatibility entry. `MinV` is a writer requirement and is intentionally ignored by both profile and server. |
| Unknown box handling, T.800 I.8 and T.801 M.12 | `boxtype` decode mapping and `other` alternatives in `TopPayload` and `InnerPayload` | `jp2-unknown-top-wxyz.jp2`, `jp2-unknown-top-binary.jp2`, `jpx-unknown-top-wxyz.jpx`, `jpx-unknown-inner-wxyz.jpx` | Not applicable | `ReadJP2` and `ReadJPX` skip unrecognized boxes within the enclosing box boundary. |
| SIZ image, tile, and component fields, T.800 Tables A.9 and A.11 | `Siz` (`SizFixed` and `Component`), `Siz-Profile` (`SizFixed-Profile` and `Component-Profile`), and the `siz.*` cross-field rules | `jp2.jp2`, `jp2-siz.rsiz-2.jp2` | `jp2-siz.xsiz-0.jp2`, `jp2-siz.csiz-2.jp2`, `jp2-siz.xtosiz-1.jp2`, `jp2-rule-siz.xsiz-52.jp2` | `ReadSIZMarker` checks field ranges and relationships. The profile additionally requires one tile, zero origins, unit component sampling, signed-32-bit dimensions, and a packet count that fits signed JPIP state. The `siz.xsiz-52` vector keeps one tile and a matching 65,536-packet PLT, so its only profile failure is the dimension limit. `Rsiz` is preserved rather than used as a blanket extension rejection. |
| COD coding style, T.800 Tables A.12 through A.21 | `Cod`, `Scod`, `Sgcod`, `Spcod`, `PrecinctSize`, `Cod-Profile` (no SOP), and `cod.*` rules | `jp2-cod.sgcod.progression-3.jp2`, `jp2-cod.spcod.cbStyle-63.jp2`, `jp2-precincts-cod.spcod.precincts.lowest.ppx-0.jp2` | `jp2-cod.sgcod.progression-5.jp2`, `jp2-cod.spcod.cbStyle-64.jp2`, `jp2-precincts-cod.spcod.precincts.higher[0].ppx-0.jp2` | `ReadCODMarker` accepts the five Part 1 progression orders, validates code-block and precinct fields, rejects reserved code-block style bits and SOP, and retains EPH support. The server derives all packet geometry from this COD. |
| MCT prerequisites, T.800 A.6.1 and G.2 | `siz.mct-components` | A normal no-MCT base and the hand-built valid-MCT fixture | `jp2-cod.sgcod.mct-1.jp2`, `jp2-rule-siz.mct-components-50.jp2` (MCT in a tile-part COD) and hand-built depth/sampling mismatches | `ReadSIZMarker` records component properties and `ReadCODMarker` validates the first three before accepting MCT. |
| Main-header marker placement, T.800 A.3 and Table A.1 | `Codestream`, `MainSegment`, `MainMarkerCode-Profile`, `codestream.one-*`, and `codestream.segment-after-sot` | `jp2-rule-main.com-21.jp2`; `jp2-main-unknown.jp2` (an `FF70` segment) is standard-invalid only | `jp2-rule-codestream.one-cod-before-sot-0.jp2`, `jp2-rule-codestream.one-qcd-before-sot-1.jp2`, `jp2-rule-codestream.segment-after-sot-3.jp2`, `jp2-main-ppt.jp2`, `jp2-main-sop.jp2`, `jp2-main-ff00.jp2`, `jp2-main-ff30-length.jp2` | `ReadCodestream` requires one main COD and QCD before the first SOT. It rejects COC and POC because they would alter packet layout and PPM (`jp2-rule-main.packet-headers-moved-48.jp2`) because it moves the packet headers out of the tile-part data. It rejects PPT, SOP and EPH, which T.800 places elsewhere, 0xFF00, which is not a marker, and 0xFF30 to 0xFF3F, which have no segment to skip (T.800 A.1.3; the segment-less form is a hand-built case). It checks the QCD and COM length ranges (T.800 Tables A.27 and A.43) and Rcom (Table A.44), skips other length-delimited main-header markers, and permits only SOT or EOC after tile-part data. |
| SOT and tile-part sequence, T.800 A.4.2 | `TilePart` and the `sot.*` rules, per tile | `jp2-sot.tnsot-0.jp2`, `jp2-rule-sot.two-tile-parts-7.jp2`, `jp2-rule-sot.tnsot-late-8.jp2`, `jp2-rule-codestream.tile-part-limit-9.jp2`; `jp2-rule-sot.two-tiles-49.jp2` is standard-valid only | `jp2-sot.isot-1.jp2` (`sot.isot-range`), `jp2-sot.tpsot-1.jp2`, `jp2-rule-sot.tnsot-inconsistent-19.jp2`, `jp2-rule-codestream.tile-part-limit-20.jp2` | `ReadSOTMarker` enforces one tile, sequential `TPsot`, consistent nonzero `TNsot`, and at most 64 tile-parts. `Psot=0` is supported only for the final tile-part, whose data runs up to the codestream's EOC without being scanned for markers, by hand-built tests because that encoding is outside the current ACN model. |
| Tile-header restrictions, T.800 A.6.1 and A.6.4 | `TileSegment` for the standard layer and PLT-only `TileSegment-Profile` | PLT-only bases | `jp2-rule-tile.header-marker-11.jp2`, `jp2-rule-tile.header-coding-default-25.jp2`, `jp2-rule-tile.cod-once-27.jp2` | The served profile rejects COD, QCD, COM, and other tile-header markers. The client receives no tile-header data-bin, so accepting layout overrides there would make server and client packet geometry disagree. |
| PLT syntax and sequence, T.800 A.7.3 | `Plt`, `Iplt`, `plt.zplt-sequence` | `jp2-precincts-rule-plt.two-markers-10.jp2`, `jp2-rule-plt.iplt-five-bytes-22.jp2`, `jp2-rule-plt.iplt-six-bytes-30.jp2` | `jp2-plt.zplt-1.jp2` and unterminated or length-mutated PLT vectors | `ReadPLTMarker` requires sequential `Zplt` and records each marker payload. `ImageIndex::GetPLTLength` validates the variable-length packet-length encoding lazily. Non-minimal encodings of up to ten bytes remain accepted because T.800 defines the represented value, not a shortest form; longer ones (`jp2-plt-iplt-eleven-bytes.jp2`) are rejected, as in the model. |
| PLT coverage and packet count, T.800 A.7.3, B.6, and B.7 | `plt.coverage`, `plt.zero-length`, `plt.packet-count`, `plt.padding-position`, and `codestream.packet-count` | All bases and complete multi-tile-part vectors; trailing zero entries after the packet set | Short and long PLT coverage, merged packet lengths, zero entries within the packet set, extra nonzero entries, and packet-count overflow | `ImageIndex::GetOffsetPacket` requires nonzero lengths for the COD-derived packets and rejects extra nonzero entries. The model counts packet entries where COD determines the layout. `CodingParameters::FillPrecinctCounts` rejects packet totals outside signed JPIP indexing. Trailing zero-valued PLT entries remain a documented compatibility exception. |
| JP2 codestream cardinality, T.800 Annex I JP2 organization | `Jp2File-Profile` and `jp2.one-codestream` | `jp2.jp2`, `jp2-precincts.jp2` | `jp2-rule-jp2.one-codestream-38.jp2` (two), `jp2-header-jp2.one-codestream-10.jp2` (none) | `ReadJP2` requires exactly one top-level `jp2c`. It reads no other box, so the profile layer keeps every other box of a .jp2 opaque (`Jp2Payload-Profile`; `jp2-rule-ftbl.one-flst-51.jp2`). Other JP2 metadata is preserved but is not a complete JP2 semantic validation. |
| JPX Reader Requirements, T.801 M.11.1 | `jpx.reader-requirements`, `Rreq` (ML, NSF and NVF as ACN determinants), `rreq.extent` | JPX bases with `rreq`, `jpx-embedded-header-rreq.mask-length-61.jpx` | `jpx-embedded-rule-jpx.reader-requirements-13.jpx`, `jpx-embedded-header-rreq.ml-62.jpx`, `-rreq.nsf`, `-rreq.nvf`, `-rreq.extent` are `invalid/valid` | The standard layer requires one `rreq` immediately after `ftyp`, with well-formed contents. esajpip ignores it and accepts its absence as a deliberate profile reduction. `hv_merge` writes it with the generated encoder (`hv_write_rreq`), and its tests check it with `hv_check_jpx_headers`. |
| JP2 header boxes, T.800 I.5.3 (JP2) and T.801 M.11.5 to M.11.7 (JPX) | `Ihdr`, `Bpcc`, `Colr`, `Pclr`, `Cmap`, `Cdef`, `Res` in `jp2-boxes.*`; the header rules at the end of `jp2-boxes.asn1` (`hv_rules.c`, standard layer only) | `jp2-header-jp2h.palette-0.jp2`, `jp2-header-cdef.opacity-49.jp2`, `jp2-header-jp2h.resolution-1.jp2`, `jp2-header-jp2.ipr-76.jp2`, `jpx-embedded-header-jpx.header-defaults-50.jpx`, `jpx-embedded-header-jpx.no-jp2h-52.jpx`, `jpx-embedded-header-colr.cielab-parameters-68.jpx` and `-colr.ciejab-parameters-69.jpx` (EP after EnumCS, T.801 M.11.7.4), `jpx-embedded-header-jpch.ipr-box-78.jpx`, `jpx-embedded-header-jplh.creg-80.jpx` | The other `jp2-header-*` and `jpx-embedded-header-*` vectors: one per rule, and field values outside the types (`ihdr.c`, `cmap.mtyp`, `res.vd`, ...) | The server reads none of these boxes and passes them to the client as metadata, so the profile keeps them opaque and every such vector is `invalid/valid`, except `jp2-header-jp2.one-codestream-10.jp2` (no `jp2c`), which is invalid at both layers. The tools that write files check them: `hv_transcode` and `hv_merge` reject an input that fails `hv_check_jp2h`, and `hv_merge`'s output passes `hv_check_jpx_headers`. |
| JPX codestream headers and numbering, T.801 M.11.6 | `jpx.codestream-count`, `jpx.no-jpch`, and source-box-order rules | `jpx-embedded.jpx`, `jpx-embedded-rule-jpx.box-order-12.jpx` | `jpx-embedded-rule-jpx.codestream-count-15.jpx`, `jpx-embedded-rule-jpx.no-jpch-17.jpx` | `ReadJPX` numbers top-level `jp2c` and `ftbl` boxes in physical order and requires one `jpch` per served codestream. The profile does not implement the standard fallback to `jp2h` when `jpch` is absent. |
| Data Reference and URL boxes, T.801 M.11.2 and Data Entry URL box, T.800 I.7.3.2 | `DataReferences`, `UrlHeader` (VERS and FLAG 0), `DataEntryUrl-Profile`, `dtbl.ndr-count`, and `url.*` | `jpx-linked.jpx` | `jpx-linked-rule-dtbl.ndr-count-5.jpx`, `jpx-linked-rule-url.version-flags-7.jpx` (VERS 1: `decode` at both layers, `url.version-flags` in the reader), `jpx-linked-rule-url.file-scheme-6.jpx`, `jpx-linked-rule-url.terminator-19.jpx` (a byte after LOC's NUL) | `ReadJPX` and `ReadUrlBox` accept one top-level `dtbl` containing version-zero, flag-zero local `file://` references, each ending at LOC's NUL; T.800 I.7.3.2 requires VERS and FLAG 0 in every Data Entry URL box. Remote URLs are outside the served profile. The model's 1,024-character LOC maximum limits corpus allocation; the server, reader, and merger impose no matching cap. |
| Fragment tables, T.801 Annex M Fragment Table box | `FragmentList`, `FragmentList-Profile`, `Fragment`, `ftbl.one-flst`, `flst.nf-count`, and `flst.dr-range` | `jpx-linked.jpx` | `jpx-linked-rule-ftbl.one-flst-4.jpx`, `jpx-linked-rule-flst.dr-range-3.jpx`, `jpx-linked-rule-flst.dr-external-2.jpx`, `jpx-linked-rule-flst.nf-count-20.jpx`, `jpx-linked-rule-flst.one-fragment-21.jpx` (a byte after the only fragment: `decode` at both layers, `flst.one-fragment` in the reader); at the edges of T.801 Table M.17, `jpx-linked-rule-flst.off-22.jpx` (OFF 11: invalid at both layers), `jpx-linked-rule-flst.source-extent-23.jpx` (LEN 0) and `jpx-linked-rule-flst.one-fragment-24.jpx` (NF 0, no fragment), both `valid/invalid` | `ReadFlstBox` requires one fragment per `ftbl`. The profile requires an external reference and later verifies the fragment against the complete codestream extent in the linked JP2. |
| Box placement, T.800 I.2 and T.801 M.11.2, M.11.3 and M.11.6 | `box.*`, `dtbl.non-url`, `jpch.nested-jp2c` (`hv_rule_child`) in the children of every top-level superbox; the model's opaque `jpch`, `ftbl`, `dtbl` and `asoc` alternatives of `InnerPayload` | `jpx-linked.jpx`, `jpx-asoc-nested.jpx` | `jpx-embedded-rule-jpch.nested-jp2c-18.jpx`, `jpx-nested-jpch.jpx`, `jpx-nested-ftbl.jpx`, `jpx-nested-dtbl.jpx`, `jpx-linked-rule-box.flst-placement-17.jpx`, `jpx-linked-rule-box.url-placement-18.jpx`; `jp2-header-box.nested-jp2c-66.jp2` and `jpx-embedded-header-box.flst-placement-67.jpx` (inside `jp2h`) are standard-invalid only | `ReadJPX` descends into top-level `jpch`, `ftbl` and `dtbl` only, and rejects `jp2c`, `jpch`, `ftbl` and `dtbl` below the top level, `flst` outside an `ftbl` and `url` outside a `dtbl`. The standard layer also checks the children of `jp2h`, `jplh`, `uinf` and `asoc`, which the server skips, but not for `url`: T.800 I.7.3 puts one in `uinf`. |
| Linked-JPX source shape | Profile rules `jpx.mixed-sources`, `jpx.linked-shape`, `url.jp2-target` | `jpx-linked.jpx` and its two companion JP2 files | `jpx-linked-rule-jpx.mixed-sources-9.jpx`, `jpx-linked-rule-jpx.linked-shape-10.jpx` (no top-level `dtbl`, fragments with DR 0: standard-valid; the DR rules come after the count rules, as in the reader), `jpx-linked-rule-url.jp2-target-8.jpx` | This is a Helioviewer served-profile decision, not a general T.801 restriction. `ReadJPX` supports either embedded codestreams or one complete linked JP2 per codestream, never a mixture or JPX-to-JPX recursion. |

## Intentional standard/profile differences

The manifest currently contains eight kinds of `invalid/valid` evidence:

- Inner box lengths inside `jp2h` are checked by the standard model but not by
  esajpip, which preserves that metadata without interpreting it. These are the
  `jp2[-precincts]-len-28-*`, `jp2[-precincts]-len-3e-*`,
  `jpx-embedded-len-40-*`, `jpx-embedded-len-56-*`, `jpx-linked-len-43-*` and
  `jpx-linked-len-59-*` vectors.
- The contents of the header boxes (`jp2h`, `jpch`, `jplh`): the
  `jp2-header-*` and `jpx-embedded-header-*` vectors that break a header rule
  or field range or put a `jp2c` or `flst` in `jp2h`, other than the `rreq`
  ones below. The server does not read them; the tools reject such inputs.
- The contents of the JPX Reader Requirements box, which the profile keeps
  opaque: `jpx-embedded-header-rreq.ml-62.jpx`,
  `jpx-embedded-header-rreq.nsf.jpx`, `jpx-embedded-header-rreq.nvf.jpx` and
  `jpx-embedded-header-rreq.extent.jpx`.
- A missing JPX Reader Requirements box is rejected by the standard layer but
  accepted by the server. The two vectors are
  `jpx-embedded-rule-jpx.reader-requirements-13.jpx` and
  `jpx-linked-rule-jpx.reader-requirements-0.jpx`.
- Zero-valued PLT entries after the logical packet set are accepted for
  deployed files (every one of 4,014 EUI files checked carries them; see
  `lib/README.md`). The vectors are
  `jp2-rule-plt.trailing-zero-16.jp2` and
  `jpx-embedded-rule-plt.trailing-zero-16.jpx`.
- Association contents are opaque to the server. `jpx-asoc-child-length.jpx`
  has a malformed inner box length and `jpx-asoc-one-child.jpx` violates the
  two-child minimum in T.801 M.11.11. The standard model rejects both; the
  profile accepts them. The server test verifies that each complete payload
  remains one metadata bin. `jpx-asoc.jpx` is valid at both layers, while
  `jpx-asoc-outer-length.jpx` exceeds the file boundary and is rejected by both.
- Boxes of a .jp2 other than the signature, file type and codestream boxes
  are opaque to the server, which reads none of them.
  `jp2-rule-ftbl.one-flst-51.jp2` holds an empty fragment table, which T.801
  forbids.
- A main-header marker segment whose code T.800 does not define: layer 1
  admits only T.800's codes, while the server and the profile
  (`MainSegment-Profile`'s `other`) skip it by length. The vectors are
  `jp2-main-unknown.jp2`, `jp2-precincts-main-unknown.jp2` and
  `jpx-embedded-main-unknown.jpx`.

`valid/invalid` vectors cover standard-permitted structures outside the served
profile and linked sources that fail cross-file checks. They cover nonzero origins, multiple tiles, component
subsampling, SOP, main or tile-header packet-layout overrides, missing PLT,
more than 64 tile-parts, oversized packet state, remote or non-JP2 links, and
general JPX organizations that esajpip does not serve.

## Coverage boundaries

The companion oracle measures codestream extents when writing each generated
JP2, then compares them with decoded `flst` claims. The four
`jpx-linked-rule-flst.source-extent-*` vectors shift the start or end by one
byte, and `jpx-linked-rule-url.missing-companion-*` names an unavailable file.
All five pass the structural standard model but fail the profile oracle and
server. The matching linked base is the accepted boundary case.

`CheckSourceForms` adds 28 explicit server cases for alternate framing and
container forms. It covers normal, zero, and extended box lengths, exact and
overrunning enclosing boundaries, truncated XLBox headers, `Psot=0` endings
and PLT coverage, recursive associations, and excluded `j2cx` storage. Every
accepted file is packet-indexed. Nested associations are also checked for
preservation as one complete metadata bin. These cases do not receive ASN.1
model labels.

The generated `jpx-graph-*` fixtures exercise sequential, reversed, and repeated
data references with distinct companion JP2 files (T.801 M.11.2 and M.11.3.1).
One companion has one packet, the other four packets across two tile-parts.
The server test checks the resolved filenames, packet counts, and exact packet
extents while interleaving forward and backward lookups across codestreams.
The repeated-reference case also leaves one data-reference entry unused.
The model checks the JPX structure; these server checks verify its resolution
against the companion files.

The generated `plt.boundaries` fixtures put packet lengths 1, 127, 128, and
129 in separate PLT markers across two tile-parts (T.800 A.7.3 and Table A.36).
The server test checks exact lengths and offsets, including backward lookups
after indexing the second tile-part. The `plt.second-part-short` and
`plt.second-part-long` variants change only the final length by one byte and
must fail during indexing. These fixtures cover both JP2 and embedded JPX.

The hand-built progression fixtures independently check the packet order from
T.800 B.12.1.1–B.12.1.5 for all five progressions, using two layers, two
resolutions, two components, and two precincts per resolution. Expected source
offsets come from explicit packet sequences rather than the server's index
formulas. They also check lazy indexing, retrieval of earlier packets, and
identical JPP responses across source progressions. This exercises packet
placement, not entropy decoding.

The generated corpus does not currently express these paths:

- `Psot=0`, `LBox=0`, and XLBox. Existing hand-built tests cover the supported
  open-ended forms and malformed container boundaries.
- Deeper association trees. Explicit server tests cover opaque preservation;
  the standard model decodes one level of children and keeps a nested
  `asoc` opaque (`jpx-asoc-nested.jpx` is valid), so what a nested
  association holds is unchecked.
- Multiple Codestream (`j2cx`) boxes and more general JPX composition
  structures. The server test checks the `j2cx` storage exclusion; full
  composition semantics remain outside this suite.
- Entropy-coded packet correctness, transforms, color interpretation, sample
  decoding, and rendering. esajpip does not perform those operations.
- JPIP request semantics and JPP-stream response framing. Those belong to the
  protocol and live-server tests, not this source-file model.

Corpus bounds such as 32 top-level boxes, 128 PLT entries, and 4 KiB opaque
payloads limit what the generator can allocate. They are not JPEG 2000 limits
and do not become server limits.

## Complementary JPP response coverage

`tests/jpp_validation.h`, run by the live-server test, independently reads
T.808 A.2 message headers and D.3 EOR messages. Sixteen runs combine embedded or
linked JPX, plain or gzip responses, absent or partial cache-model claims, and
`stream` or `context` selection. Each codestream has 130 precincts, but their
widths, precinct sizes, and layer counts differ. Large final-precinct packets
exercise multi-byte offsets and lengths as well as Bin-ID continuation bytes.
Small response budgets and the server's 128-byte test chunks split packets
across chunks and responses.

The test compares every payload with its source bytes, reconstructs main-header,
tile-header, precinct, and metadata bins, and checks offsets, completion flags,
EOR reasons, and the absence of retransmission after the window is complete.
Expected metadata includes independently built codestream and association
placeholders, with a nested association preserved in a separate metadata bin.
Partial `Hm` and `P` models seed known prefixes before transfer.

Two additional stateful runs, one per source form, cover:

- `len=0`, `1`, and `2`, absent `len`, and metadata-only requests;
- partial `M0`/`M1` models, including a prefix ending inside a placeholder;
- single-pixel precinct boundaries, an empty intersection, backward scrubbing,
  overlapping selections, and default window offset and size;
- cache continuity after TCP reconnection and pipelined cached requests;
- a new channel on an existing socket, complete-bin claims, independent
  channel caches, and continued use after closing the other channel;
- discarding cache claims under a mismatched target ID.

Each window permits only its independently selected bins. Revisiting completed
precincts must not resend bytes, including after cache-prefix compaction.
Reader self-tests cover header inheritance, defaults resetting between
responses, and rejection of truncated, overflowing, reserved, misplaced, and
incorrect-payload messages.

This is a served-profile response oracle, not a general JPIP decoder. It does
not validate entropy-coded samples or every legal extended message class.
The separate real-data check on September 19, 2026 used 100 timestamp-ordered
EUI FSI frames in a linked JPX. Headless JHV loaded the JPIP URL and recorded
through SAMP, reporting completion. `ffprobe` confirmed 100 frames at
4096 × 4096. Neither server nor JHV logs contained a serving or decoding error.
This run does not establish the 4,014-frame or Debian gates.

## Keeping this map accurate

When a model or parser rule changes:

1. Regenerate the corpus and review every manifest label change.
2. Run `jpeg2000_test`, which checks opening and every declared packet for all
   profile-valid rows and confirms rejection of every profile-invalid row.
3. Update this map when a rule class, served-profile decision, or coverage
   boundary changes. Individual edge vectors remain discoverable in the
   manifest and do not need separate rows here.
