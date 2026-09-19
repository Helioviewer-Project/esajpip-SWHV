# JPEG 2000 source-profile coverage

This map connects the rules in the ASN.1/ACN model to generated vectors and
the esajpip source that enforces the served profile. It answers a narrower
question than a JPEG 2000 decoder conformance suite: does esajpip accept the
file structures it documents and reject malformed or unsupported structures?

The authoritative list of individual vectors and labels is
`tests/vectors/j2k/manifest.tsv`. A label is written as `standard/profile`.
For example, `valid/invalid` means that the structure is permitted by the
cited JPEG 2000 standard but deliberately excluded from the esajpip source
profile. Vector names below are representative. The manifest-driven test runs
all of them.

## Coverage map

| Structure and reference | Model and rule | Accepted evidence | Rejected evidence | Server enforcement and profile decision |
| --- | --- | --- | --- | --- |
| Box lengths and containment, T.800 Annex I and T.801 Annex M | `TopBox`, `InnerBox`, ACN `lbox`, and `size deduced` regions in `jp2-boxes.*` | The four base files and all unchanged structural mutants | `*-len-*` vectors for top-level boxes and interpreted superboxes | `ReadBoxHeader` bounds each box against its enclosing container. Metadata boxes that esajpip skips as a whole, including `jp2h`, `jplh`, and `uinf`, are opaque in the profile model. The current inner-length mutants exercise `jp2h`: they are standard-invalid but profile-accepted. |
| JP2 signature, T.800 I.4 and I.5.1 | `file.signature` in `crossfield_impl.h` | `jp2.jp2` | `jp2-sig-bad.jp2`, `jp2-rule-file.signature-37.jp2` | `ReadImage` checks the complete 12-byte signature before dispatching by extension. |
| File Type box, T.800 I.5.2 and T.801 M.8 | `Ftyp`, `file.ftyp-brand`, and `file.ftyp-compatibility` | `jp2-rule-file.ftyp-compatibility-36.jp2` | `jp2-rule-file.ftyp-brand-34.jp2`, `jp2-rule-file.ftyp-compatibility-35.jp2` | `ReadFileType` requires the extension-specific brand and a matching compatibility entry. `MinV` is a writer requirement and is intentionally ignored by both profile and server. |
| Unknown box handling, T.800 I.8 and T.801 M.12 | `other` alternatives in `TopPayload` and `InnerPayload` | `jp2-rule-file.unknown-box-14.jp2`, `jpx-linked-rule-file.unknown-box-1.jpx` | Not applicable | `ReadJP2` and `ReadJPX` skip unrecognized boxes within the enclosing box boundary. |
| SIZ image, tile, and component fields, T.800 Tables A.9 and A.11 | `Siz`, `Component`, and the `siz.*` cross-field rules | `jp2.jp2`, `jp2-siz.rsiz-2.jp2` | `jp2-siz.xsiz-0.jp2`, `jp2-siz.csiz-2.jp2`, `jp2-siz.xtosiz-1.jp2` | `ReadSIZMarker` checks field ranges and relationships. The profile additionally requires one tile, zero origins, unit component sampling, signed-32-bit dimensions, and a packet count that fits signed JPIP state. `Rsiz` is preserved rather than used as a blanket extension rejection. |
| COD coding style, T.800 Tables A.12 through A.21 | `Cod`, `Scod`, `Sgcod`, `Spcod`, `PrecinctSize`, and `cod.*` rules | `jp2-cod.sgcod.progression-3.jp2`, `jp2-cod.spcod.cbStyle-63.jp2`, `jp2-precincts-cod.spcod.precincts.lowest.ppx-0.jp2` | `jp2-cod.sgcod.progression-5.jp2`, `jp2-cod.spcod.cbStyle-64.jp2`, `jp2-precincts-cod.spcod.precincts.higher[0].ppx-0.jp2` | `ReadCODMarker` accepts the five Part 1 progression orders, validates code-block and precinct fields, rejects reserved code-block style bits and SOP, and retains EPH support. The server derives all packet geometry from this COD. |
| MCT prerequisites, T.800 A.6.1 and G.2 | `siz.mct-components` | A normal no-MCT base and the hand-built valid-MCT fixture | `jp2-cod.sgcod.mct-1.jp2` and hand-built depth/sampling mismatches | `ReadSIZMarker` records component properties and `ReadCODMarker` validates the first three before accepting MCT. |
| Main-header marker placement, T.800 A.3 and Table A.1 | `Codestream`, `MainSegment`, `codestream.one-*`, and `codestream.segment-after-sot` | `jp2-rule-main.com-21.jp2` | `jp2-rule-codestream.one-cod-before-sot-0.jp2`, `jp2-rule-codestream.one-qcd-before-sot-1.jp2`, `jp2-rule-codestream.segment-after-sot-3.jp2` | `ReadCodestream` requires one main COD and QCD before the first SOT. It rejects COC and POC because they would alter packet layout, skips other length-delimited main-header markers, and permits only SOT or EOC after tile-part data. |
| SOT and tile-part sequence, T.800 A.4.2 | `TilePart` and the `sot.*` rules | `jp2-sot.tnsot-0.jp2`, `jp2-rule-sot.two-tile-parts-7.jp2`, `jp2-rule-sot.tnsot-late-8.jp2`, `jp2-rule-codestream.tile-part-limit-9.jp2` | `jp2-sot.tpsot-1.jp2`, `jp2-rule-sot.tnsot-inconsistent-19.jp2`, `jp2-rule-codestream.tile-part-limit-20.jp2` | `ReadSOTMarker` enforces one tile, sequential `TPsot`, consistent nonzero `TNsot`, and at most 64 tile-parts. `Psot=0` is supported only for the final tile-part by a hand-built test because that encoding is outside the current ACN model. |
| Tile-header restrictions, T.800 A.6.1 and A.6.4 | `TileSegment` for the standard layer and PLT-only `TileSegment-Profile` | PLT-only bases | `jp2-rule-tile.header-marker-11.jp2`, `jp2-rule-tile.header-coding-default-25.jp2`, `jp2-rule-tile.cod-once-27.jp2` | The served profile rejects COD, QCD, COM, and other tile-header markers. The client receives no tile-header data-bin, so accepting layout overrides there would make server and client packet geometry disagree. |
| PLT syntax and sequence, T.800 A.7.3 | `Plt`, `Iplt`, `plt.zplt-sequence` | `jp2-precincts-rule-plt.two-markers-10.jp2`, `jp2-rule-plt.iplt-five-bytes-22.jp2`, `jp2-rule-plt.iplt-six-bytes-30.jp2` | `jp2-plt.zplt-1.jp2` and unterminated or length-mutated PLT vectors | `ReadPLTMarker` requires sequential `Zplt` and records each marker payload. `ImageIndex::GetPLTLength` validates the variable-length packet-length encoding lazily. Non-minimal encodings remain accepted because T.800 defines the represented value, not a shortest form. |
| PLT coverage and packet count, T.800 A.7.3, B.6, and B.7 | `plt.coverage`, `plt.zero-length`, and `codestream.packet-count` | All bases and complete multi-tile-part vectors | `jp2-rule-plt.sum-exceeds-data-23.jp2`, `jp2-rule-plt.sum-short-24.jp2`, `jp2-rule-codestream.packet-count-29.jp2` | `ImageIndex::GetOffsetPacket` requires PLT lengths to cover tile-part data and the COD-derived packet set exactly. `CodingParameters::FillPrecinctCounts` rejects packet totals outside signed JPIP indexing. Trailing zero-valued PLT entries are a documented compatibility exception. |
| JP2 codestream cardinality, T.800 Annex I JP2 organization | `Jp2File-Profile` and `jp2.one-codestream` | `jp2.jp2`, `jp2-precincts.jp2` | `jp2-rule-jp2.one-codestream-38.jp2` | `ReadJP2` requires exactly one top-level `jp2c`. Other JP2 metadata is preserved but is not a complete JP2 semantic validation. |
| JPX Reader Requirements, T.801 M.11.1 | `jpx.reader-requirements` | JPX bases with `rreq` | `jpx-embedded-rule-jpx.reader-requirements-13.jpx` is `invalid/valid` | The standard layer requires one `rreq` immediately after `ftyp`. esajpip ignores it and accepts its absence as a deliberate profile reduction. |
| JPX codestream headers and numbering, T.801 M.11.6 | `jpx.codestream-count`, `jpx.no-jpch`, and source-box-order rules | `jpx-embedded.jpx`, `jpx-embedded-rule-jpx.box-order-12.jpx` | `jpx-embedded-rule-jpx.codestream-count-15.jpx`, `jpx-embedded-rule-jpx.no-jpch-17.jpx` | `ReadJPX` numbers top-level `jp2c` and `ftbl` boxes in physical order and requires one `jpch` per served codestream. The profile does not implement the standard fallback to `jp2h` when `jpch` is absent. |
| Data Reference and URL boxes, T.801 M.11.2 and Data Entry URL box | `DataReferences`, `DataEntryUrl`, `dtbl.ndr-count`, and `url.*` | `jpx-linked.jpx` | `jpx-linked-rule-dtbl.ndr-count-5.jpx`, `jpx-linked-rule-url.version-7.jpx`, `jpx-linked-rule-url.file-scheme-6.jpx` | `ReadJPX` and `ReadUrlBox` accept one top-level `dtbl` containing version-zero, flag-zero local `file://` references. Remote URLs are outside the served profile. |
| Fragment tables, T.801 Annex M Fragment Table box | `FragmentList`, `Fragment`, `ftbl.one-flst`, and `flst.dr-range` | `jpx-linked.jpx` | `jpx-linked-rule-ftbl.one-flst-4.jpx`, `jpx-linked-rule-flst.dr-range-3.jpx`, `jpx-linked-rule-flst.dr-external-2.jpx` | `ReadFlstBox` requires one fragment per `ftbl`. The profile requires an external reference and later verifies the fragment against the complete codestream extent in the linked JP2. |
| Linked-JPX source shape | Profile rules `jpx.mixed-sources`, `jpx.linked-shape`, `url.jp2-target` | `jpx-linked.jpx` and its two companion JP2 files | `jpx-linked-rule-jpx.mixed-sources-9.jpx`, `jpx-linked-rule-jpx.linked-shape-10.jpx`, `jpx-linked-rule-url.jp2-target-8.jpx` | This is a Helioviewer source-profile decision, not a general T.801 restriction. `ReadJPX` supports either embedded codestreams or one complete linked JP2 per codestream, never a mixture or JPX-to-JPX recursion. |

## Intentional standard/profile differences

The manifest currently contains three kinds of `invalid/valid` evidence:

- Inner box lengths inside `jp2h` are checked by the standard model but not by
  esajpip, which preserves that metadata without interpreting it. These are the
  `jp2[-precincts]-len-28-*` and `jp2[-precincts]-len-3e-*` vectors.
- A missing JPX Reader Requirements box is rejected by the standard layer but
  accepted by the server. The two vectors are
  `jpx-embedded-rule-jpx.reader-requirements-13.jpx` and
  `jpx-linked-rule-jpx.reader-requirements-0.jpx`.
- Zero-valued PLT entries after the logical packet set are accepted for
  deployed AIA-derived files. The vectors are
  `jp2-rule-plt.trailing-zero-16.jp2` and
  `jpx-embedded-rule-plt.trailing-zero-16.jpx`.

`valid/invalid` vectors are standard-permitted inputs deliberately outside the
served profile. They cover nonzero origins, multiple tiles, component
subsampling, SOP, main or tile-header packet-layout overrides, missing PLT,
more than 64 tile-parts, oversized packet state, remote or non-JP2 links, and
general JPX organizations that esajpip does not serve.

## Coverage boundaries

The generated corpus does not currently express these paths:

- `Psot=0`, `LBox=0`, and XLBox. Existing hand-built tests cover the supported
  open-ended forms and malformed container boundaries.
- Association (`asoc`) contents are parsed as a superbox by both model layers,
  while esajpip exposes the entire payload as one opaque metadata bin. No
  generated `asoc` vector currently exercises that difference.
- The linked fragment's offset and length matching the referenced JP2
  codestream. That needs both files and remains covered by the hand-built linked
  JPX tests.
- Deeper association trees, Multiple Codestream (`j2cx`) boxes, and more
  general JPX composition structures. They remain explicit profile exclusions.
- Entropy-coded packet correctness, transforms, color interpretation, sample
  decoding, and rendering. esajpip does not perform those operations.
- JPIP request semantics and JPP-stream response framing. Those belong to the
  protocol and live-server tests, not this source-file model.

Corpus bounds such as 32 top-level boxes, 128 PLT entries, and 4 KiB opaque
payloads limit what the generator can allocate. They are not JPEG 2000 limits
and do not become server limits.

## Keeping this map accurate

When a model or parser rule changes:

1. Regenerate the corpus and review every manifest label change.
2. Run `jpeg2000_test`, which checks opening and every declared packet for all
   profile-valid rows and confirms rejection of every profile-invalid row.
3. Update this map when a rule class, served-profile decision, or coverage
   boundary changes. Individual edge vectors remain discoverable in the
   manifest and do not need separate rows here.
