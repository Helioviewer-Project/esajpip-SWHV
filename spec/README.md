# JPEG 2000 file model and test-vector corpus

## Start here

esajpip reads JPEG 2000 files (`.jp2`, `.jpx`) and serves pieces of them to
JHelioviewer over JPIP. The code that reads those files is
`src/jpeg2000/file_manager.cc`. It has to reject malformed input without
crashing, and it has to accept exactly the files the project promises to
support ([`JPIP_PROFILE.md`](../JPIP_PROFILE.md), "Supported JPEG 2000
sources").

This directory is a **formal description of what a valid file looks like**,
written in a machine-readable notation (ASN.1 with ACN encoding rules), plus
a small program that turns that description into a **corpus of test files**:
hundreds of tiny `.jp2`/`.jpx` files, each labelled "the server must
accept this" or "the server must reject this". `tests/jpeg2000_test.cc` then
opens every one and checks that the server agrees.

The point of doing it this way rather than writing test files by hand:

- The labels are evaluated mechanically from the model and its cross-field
  rules rather than assigned to individual vectors by hand. The model and
  rules are still human-authored and must be checked against the cited
  standards. Every field of every marker segment and box gets a test at each
  edge of its allowed range, systematically, which nobody does by hand.
- The description exists in two layers — *what the standard allows* and
  *what esajpip supports* — so a test failure tells you which of the two
  the parser disagrees with, and cites the clause of the standard.
- The description is the specification. [`JPIP_PROFILE.md`](../JPIP_PROFILE.md)
  says in prose what the server accepts; `spec/*.asn1` says it in a form a
  test can prove.

Nothing here is part of the server build. The ASN.1 compiler runs offline,
on a developer machine, only when the description changes. The server tests
consume only the committed corpus through plain CMake/CTest; generated compiler
code remains outside the server build.

**Status.** With the upstream compiler revision pinned here, the complete model
generates C, that C builds as strict C11, and the sanitized harness writes the
corpus with unique names and no label mismatches.
`spec/VERSION` pins the upstream compiler source. The corpus is committed under
`tests/vectors/j2k/`, and `jpeg2000_test.cc` checks every manifest row against
the server parser and lazy packet indexer.

If you are here to **run the current server tests**, use `./tests/run.sh` and
the [test guide](../tests/README.md). You need nothing from this directory.
If you are here to **change what the server accepts**, read
"Background", then "Quick start", then edit the model and regenerate. If a
corpus test **just failed**, go to "When a vector fails".

## Scope and development order

This suite is intended to establish that esajpip accepts the JPEG 2000 source
profile it documents and rejects malformed or unsupported structures for the
right reason. It can provide strong evidence for the parts of T.800 and T.801
that the server parses: boxes, marker framing, coding parameters, PLT packet
lengths, tile-part structure, fragment tables, data references, and the
cross-field relationships between them. The separate standard and profile
layers also make an intentional reduction or leniency visible instead of
silently treating current parser behaviour as the standard.

It is not a complete JPEG 2000 conformance suite. In particular, it does not
validate entropy-coded packet contents, inverse transforms, color processing,
decoded samples, rendering, or every legal JPX organization. esajpip does not
perform those operations. A passing corpus means that the server agrees with
the modelled structural rules and its declared source profile; it does not mean
that an arbitrary JPEG 2000 decoder is conformant.

The model, corpus integration, and coverage map are in place. The coverage
expansion now tests progression order independently, PLT and tile-part
boundaries, linked-file reference graphs and fragment extents, opaque metadata,
and alternate box encodings. The latter use explicit server fixtures where
the current ACN model cannot express the wire form. Remaining extensions should
follow this order when a concrete need justifies them:

1. Keep the deferred-ACN compiler regressions passing and the compiler revision
   pinned in `spec/VERSION`. Build the generated code as strict C11 and run the
   harness in Linux Docker with AddressSanitizer and UndefinedBehaviorSanitizer
   whenever the model changes. Do not hand-edit generated output or corpus
   labels.
2. Expand the highest-value production paths first: packet indexing across
   progression orders, layers, precincts, PLT segments and tile-parts, followed
   by complete linked-JPX graphs and their companion JP2 files.
3. Then model additional valid-but-unsupported forms such as `Psot = 0`,
   `LBox = 0`, XLBox, deeper association trees, Multiple Codestream boxes, and
   more general JPX layouts. The result may be an explicit profile rejection;
   modeling a form does not oblige the server to support it.
4. Keep extensions to T.808 requests, JPIP channel state, JPP-stream framing,
   hvJP2K output, and decoder interoperability as separate test layers. Reuse
   this corpus where useful, but do not make the source-file model responsible
   for HTTP, session, or image-decoding behaviour.

Every new rule needs at least one accepted vector at or near its boundary and
one rejected vector that violates only that rule. Each must cite the applicable
standard clause, state whether it belongs to the standard or server-profile
layer, and exercise `GetPacket` when rejection can occur during lazy indexing.
Independent tools such as jpylyzer, OpenJPEG, Kakadu, or Grok are useful for
comparison, but none replaces the cited standard plus the model as the expected
result.

## Background you need

### A JPEG 2000 file, in one picture

A `.jp2` or `.jpx` file is a sequence of **boxes**. A box is a 4-byte length
`LBox`, a 4-byte type (four ASCII characters), and a payload. Some boxes
(superboxes) contain more boxes. One box type, `jp2c`, contains a
**codestream**, which is the actual compressed image.

```
.jp2 file
├── jP      signature box (must be first; contents 0D 0A 87 0A)
├── ftyp    file type box (must be second)
├── jp2h    superbox: ihdr (image size), colr (colour space), ...
└── jp2c    the codestream
            ├── SOC             marker: start of codestream
            ├── SIZ  segment    image and tile size, components
            ├── COD  segment    coding style: decomposition levels, precincts, ...
            ├── QCD  segment    quantization (esajpip skips its contents)
            ├── SOT  segment    start of a tile-part; Psot = its total length
            │   ├── PLT segment packet lengths (esajpip needs at least one)
            │   ├── SOD         marker: start of data
            │   └── ...         packet data, up to the Psot boundary
            └── EOC             marker: end of codestream
```

A codestream is a sequence of **marker segments**: a 2-byte marker code
(`FF51` = SIZ, `FF52` = COD, …), a 2-byte length `Lxxx` that counts itself,
and a body. The main header runs from SOC to the first SOT; then one or
more tile-parts; then EOC.

A `.jpx` movie for JHelioviewer usually has **no codestreams in it at all**:
it has one `jpch` box per frame, one `ftbl`/`flst` box per frame pointing
into a separate `.jp2` file (an `flst` "fragment" = file offset + length +
which file), and one `dtbl` box holding the `url` boxes that name those
files. That is the "linked JPX" form and the production workload.

The formal references are ITU-T T.800 (JPEG 2000 Part 1: Annex A is the
codestream, Annex I is the JP2 file format) and T.801 (Part 2: Annex M has
the JPX boxes). The model files cite the relevant table or clause on every
field, so you rarely need the standards themselves. Local copies live in
`standards/`, which is outside Git.

The standard layer requires the Reader Requirements box that T.801 M.11.1
places immediately after `ftyp`; the profile layer accepts its absence because
the server ignores the box. `MinV` is not constrained because it is a writer
requirement (0 for JP2 and 1 for JPX) that readers are required to tolerate.
Generated base vectors nevertheless carry the conforming `MinV` and a minimal
`rreq` box, so they are valid at both layers.

### ASN.1 and ACN, in ten lines

**ASN.1** is a notation for describing structured data: records
(`SEQUENCE`), one-of-several (`CHOICE`), lists (`SEQUENCE OF`), byte strings
(`OCTET STRING`), integers with allowed ranges (`INTEGER (1..16384)`). It
describes *what the values are*, not how they are laid out in bytes.

**ACN** is a companion notation, from ESA, that says *how the bytes are laid
out*: this integer is 16 bits big-endian; this list has as many elements as
that earlier field says; this field is present only when that flag is set;
this list fills whatever is left of the enclosing region (`size deduced`).
With ACN you can describe an existing binary format exactly, byte for byte.

**asn1scc** is ESA's compiler for both. From a `.asn1` file and its `.acn`
file it generates C: a struct per type, an encoder (struct → bytes), a
decoder (bytes → struct, refusing anything that does not match the layout),
and a constraint checker (does every integer sit in its allowed range?).
That generated decoder is what labels the corpus.

The two notations look like this (from `j2k-headers.asn1` / `.acn`):

```asn1
Sgcod ::= SEQUENCE {                    -- ASN.1: the fields and their ranges
    progression     INTEGER (0..4),     -- Table A.16: LRCP RLCP RPCL PCRL CPRL
    layers          INTEGER (1..65535),
    mct             INTEGER (0..1)
}
```

```
Sgcod [] {                              -- ACN: the byte layout of the same fields
    progression     [encoding pos-int, size 8],
    layers          [encoding pos-int, size 16, endianness big],
    mct             [encoding pos-int, size 8]
}
```

### Vocabulary

| Term | Meaning here |
| --- | --- |
| **model** | The three `spec/*.asn1` files and their `*.acn` companions, together. |
| **layer 1 / standard** | Types with the ranges and rules of T.800/T.801. |
| **layer 2 / profile** | `*-Profile` types and rules: normally layer 1 narrowed to what esajpip serves, with explicit documented leniencies matching deployed server behavior. |
| **vector** | One generated `.jp2`/`.jpx` file in the corpus. |
| **label** | The vector's verdict at each layer: `valid` or `invalid`. |
| **base** | One of four canonical, valid files everything else is derived from. |
| **mutant** | A base with one deliberate change (a field out of range, a rule broken, a length off by one). |
| **cross-field rule** | A validity rule ASN.1 cannot state (e.g. "csiz equals the number of components"); listed at the end of each `.asn1`, implemented in `harness/crossfield.c`. |
| **determinant** | An ACN field whose value controls the size or presence of another (e.g. `Lxxx` sizes the segment body). |
| **harness** | `spec/harness/`: the offline C program that builds and labels the corpus. |
| **manifest** | `tests/vectors/j2k/manifest.tsv`: one row per vector with its labels. |

## Files

| File | Role |
| --- | --- |
| `j2k-headers.asn1` / `.acn` | Marker segment bodies: SIZ, COD, QCD, PLT (with its packet-length entries, `Iplt`), COM. |
| `j2k-codestream.asn1` / `.acn` | Codestream framing: SOC, main header, tile-parts (SOT, tile headers, SOD, data), EOC. Imports the bodies. |
| `jp2-boxes.asn1` / `.acn` | JP2/JPX box tree; `jp2c` carries a full codestream; `jpch`/`ftbl`/`flst`/`dtbl`/`url`/`asoc` in full, other boxes opaque. Imports the codestream. |
| `jpeg2000-io.asn1` / `.acn` | Header types for the reader/writer in `transcode/`: box header (LBox, TBox, XLBox), marker code, Lxxx, SOT, and the SIZ/COD/QCD/PLT/COM segments at the standard's bounds (`*Segment-Std`). Decoded one at a time; lengths are ASN.1 fields, so `LBox = 0`, `LBox = 1` with XLBox, and `Psot = 0` are all expressible. Not used by the corpus harness. |
| `VERSION` | The exact upstream asn1scc revision used to generate the corpus. |
| `asn1scc-patches/` | Reference archive of the former local compiler fixes. Its README records the original base commit; the current build does not apply these patches. |
| `build-asn1scc.sh` | Exports `VERSION` from a local compiler repository into a temporary clean tree, builds the Docker image, and runs upstream ACN v2 regressions. |
| `check-model.sh` | Generates the complete model, builds it as strict C11 with ASan/UBSan, runs the corpus harness, and rejects duplicate vector names. |
| `COVERAGE.md` | Maps modeled T.800/T.801 rules to corpus evidence, server enforcement, deliberate profile decisions, and remaining boundaries. |
| `harness/vectors.c` | The generator: builds bases, derives mutants, labels, writes files and manifest. |
| `harness/crossfield*.{h,c}` | The cross-field rules, written once and instantiated for both layers' struct types. |
| `harness/mapping.{h,c}` | Three tiny functions ACN needs because `Lxxx`, `Psot` and `LBox` count more than the payload (+2, +12, +8). |
| `../tests/vectors/j2k/` | The committed corpus: the vectors plus `manifest.tsv`. |

## How the pieces fit

```
   spec/*.asn1 + *.acn  ──asn1scc (offline)──▶  generated C: structs, encoder,
                                                 decoder, constraint checker
                                                          │
   harness/vectors.c  ─── builds 4 base files as structs ─┤
                      ─── mutates them ───────────────────┤
                      ─── encodes ─▶ bytes ─▶ decodes ────┘──▶ label per layer
                      ─── + harness/crossfield.c rules ──────▶ (valid/invalid)
                                                          │
                                        tests/vectors/j2k/*.jp2, *.jpx, manifest.tsv
                                                          │
   tests/jpeg2000_test.cc ── OpenImage + GetPacket on each ▶ must match the label
```

Two facts make the generated code a good judge of validity. First, the
model puts every length (`Lxxx`, `Psot`, `LBox`) under ACN's control:
the encoder computes them, the decoder enforces them, and a body that does
not exactly fill its segment is a decode error (`size deduced`). No harness
code ever computes a length. Second, placement rules are types: a PLT in
the main header or an `flst` outside an `ftbl` cannot even be expressed, so
the decoder rejects it.

## Quick start (regenerating the corpus)

Run this workflow when a model or harness changes, or when updating the
compiler pin. Everything runs offline. Run these commands from the repository
root.

1. Get an asn1scc repository containing the commit pinned in `spec/VERSION`.
   Its checked-out branch and working tree do not matter: the build script
   exports that upstream commit to a clean tree, builds the compiler in Linux
   Docker, and runs the relevant upstream regressions:

   ```sh
   spec/build-asn1scc.sh ~/git/asn1scc esajpip-asn1scc
   ```

   Advance the pin only deliberately, then regenerate and review the corpus
   in the same change.
2. Run the complete generation and harness gate:

   ```sh
   ASN1SCC_IMAGE=esajpip-asn1scc spec/check-model.sh
   ```

   To retain the generated corpus, pass an empty output directory.
   It reports how many vectors were written and how many are valid at both
   layers. It exits non-zero if any mutant did not produce the label its
   table entry expects (see "What the harness generates"); each such line names the
   vector, the expected and actual labels, and the rule that fired. On the
   first run after an intentional model change, investigate any mismatch
   before trusting the corpus.

   The generic compiler `-atc` generator is intentionally not used for this
   model: its large bounded, inline container types make generic exhaustive
   values impractical, and it cannot replace the JPEG 2000 cross-field and
   mutation checks. Small compiler regressions cover the backend mechanisms;
   `check-model.sh` covers their composition in the real model.
3. Run the server tests and read "When a vector fails" for anything red:

   ```sh
   ./tests/run.sh
   ```
4. Commit the changed model or harness, `spec/VERSION` if it changed, and
   `tests/vectors/j2k/` together. Never edit vector files or manifest labels
   by hand.

## Reading a model file

Each `.asn1` has the same shape: a header comment stating scope and design,
**Layer 1** types with the standard's ranges and a table/clause citation on
every field, **Layer 2** `*-Profile` types, and at the end the cross-field
rules for each layer. The `.acn` mirrors the type names and gives each
field its width and encoding, plus the determinants.

Two conventions worth knowing before you edit:

- **Corpus bounds.** Some `SIZE` upper bounds are far smaller than the
  standard's and are marked "corpus bound" (e.g. 32 top-level boxes, 128 PLT
  entries). asn1scc allocates every list at its maximum inside the struct,
  so standard-sized bounds would make a `Jp2Family` gigabytes large. A file
  that exceeds a corpus bound is not standard-invalid; the harness never
  generates one. The bodies with a corpus bound (`Qcd`, `Plt`, `Com`) are
  instances of parameterized types (`QcdBody`, `PltBody`, `ComBody`); the
  `-Std` instances (`Qcd-Std`, `Plt-Std`, `Com-Std`) carry the standard's
  bounds for the reader/writer, which decodes one segment at a time. A
  `WITH COMPONENTS` subtype cannot do this: asn1scc keeps the base type's
  allocation for it.
- **Layer 2 normally narrows layer 1.** A `*-Profile` type is either a `WITH
  COMPONENTS` subtype (ranges narrowed) or, where framing has to be repeated
  with profile bodies inside `CONTAINING`, a copy with profile types
  substituted. It has fewer `CHOICE` alternatives or a smaller marker-code
  value set where the server rejects a whole class (`TileBody-Profile` is
  `plt` only; `MainMarkerCode-Profile` excludes COC and POC). The deliberate
  type-level exception is an `other` alternative for marker codes the profile
  parser skips as opaque data. Cross-field rules also express the two deployed
  leniencies: missing JPX `rreq` and trailing zero-valued PLT entries. An
  unlisted marker can therefore be `standard=invalid, profile=valid`. This
  records the server's skip policy; it does not establish that the marker is
  forbidden by every edition or extension of JPEG 2000. Unknown box types are
  valid at both layers, as required by T.800 I.8 and T.801 M.12.
- **ACN properties are explicit on profile structures.** asn1scc does not
  inherit field encodings through `WITH COMPONENTS` constraints. Profile
  structures that must be encoded independently therefore repeat the base
  structure and have their own ACN entry. This is deliberate duplication at
  the wire-description boundary, not a second interpretation of the format.

## The test contract

For every vector, `jpeg2000_test` reads the committed file and its companions
from the corpus directory under the names in its manifest row, calls
`FileManager::OpenImage`, and — if that succeeds — indexes every declared
packet of every codestream with `GetPacket`.
"Accept" means both succeed; "reject" means either fails. The second step
matters because the server parses PLT entries lazily: a malformed or
over-long packet length is only detected when a packet is indexed.

| Standard | Profile | The server must |
| --- | --- | --- |
| valid | valid | accept |
| valid | invalid | reject, for the profile reason |
| invalid | (invalid) | reject |
| invalid | valid | accept only for an explicit documented profile leniency; otherwise investigate the model |

A vector is *valid at a layer* when all three hold: the generated ACN decoder
accepts it (this is what catches region overruns, leftover bytes, wrong
`CHOICE` selection, and unknown codes), the generated constraint checker for
that layer accepts the decoded value (`<Type>_IsConstraintValid`), and the
cross-field rules for that layer hold (`crossfield.c`). The compiler performs
the evaluation, but the ASN.1/ACN model and the imperative cross-field rules
are human-maintained sources that cite the corresponding standard clauses.

The corpus tests *acceptance* (open plus complete packet indexing), not
serving. Packet data in generated files is arbitrary bytes; nothing here
claims a file decodes to an image.

Manifest columns: `file kind standard profile reason field note
companions`. `reason` is the first check that failed at the stricter
failing layer — `decode`, `constraint`, or a cross-field rule name such as
`plt.coverage` — or `-` for a valid vector; `field` names the mutated field
or rule (or `-` for a base); `note` is the mutant's intent in words;
`companions` lists files that must sit next to the vector (the `.jp2`
frames a linked JPX points at).

## When a vector fails

`ctest` reports `vector <name>: expected accept` or `expected reject`. Look
the name up in `manifest.tsv`:

- **`standard=valid profile=valid`, server rejected.** Either the parser is
  too strict, or the model is wrong about the standard. Read the `note`,
  find the field in the `.asn1`, read the cited table. If the standard
  agrees with the model, fix the parser; if not, fix the model and
  regenerate.
- **`standard=valid profile=invalid`, server accepted.** The parser is more
  lenient than `JPIP_PROFILE.md` promises. Either tighten the parser or
  change the profile (both the doc and the `*-Profile` type).
- **`standard=invalid`, server accepted.** First decide whether layer 1 is
  incomplete or the standard truly forbids the file. If the model is
  incomplete, correct it with a citation and regenerate. If the standard
  forbids the file, either tighten the parser or document a deliberate server
  leniency in `JPIP_PROFILE.md`; do not make the standard layer call it valid.
- **`standard=invalid profile=valid`.** This is a deliberate server leniency,
  such as an unknown marker code or a JPX file without `rreq`. Confirm that
  the standard citation and profile rationale are both explicit; otherwise
  investigate the model. Unknown box types are valid at both layers because
  T.800 I.8 and T.801 M.12 require readers to skip them.

The `field` column also tells you which of the harness's mutation classes
produced the vector (see "What the harness generates"), which narrows down
the parser code involved: a length mutant points at `ReadBoxHeader` /
`ReadCodestream`'s limit checks, a field mutant at the corresponding
`Read*Marker`, a rule mutant at the structural checks.

Two things that look unusual are intentional. An `Iplt` written in more
7-bit groups than its value needs is valid at both layers: the server bounds
packet lengths by value, not by byte count (`plt.iplt-five-bytes`,
`plt.iplt-six-bytes`). A zero-valued `Iplt` entry after the last packet is
standard-invalid but profile-valid because the server accepts it in deployed
files (`plt.trailing-zero`). The model counts nonzero entries against the
COD-derived packet set and rejects a zero entry within that set or a nonzero
entry after it, even when the PLT lengths still cover the tile-part data.

## Compiler checks

The compiler is built from the exact upstream revision in `spec/VERSION`.
Issue [#415](https://github.com/esa/asn1scc/issues/415) added cases under
asn1scc's `v4Tests/test-cases/acn/25-ACNV2-BOUNDARIES/` for the backend
mechanisms exposed by this model:

1. a one-bit Boolean determinant produced inside a referenced structure and
   consumed by an optional sibling;
2. constrained `CONTAINING` specializations that would otherwise collide in
   their generated C names;
3. a length determinant crossing nested `SEQUENCE` and `CHOICE` boundaries;
4. a mapped length determinant crossing a parameterized `CHOICE` boundary.

The compiler converts those cross-scope determinant relationships into
explicit ACN parameters, reserves and patches producer fields at the scope
that owns the value, and preserves mapping functions when patching measured
lengths. `build-asn1scc.sh` runs the upstream C cases and byte-exact wire
checks. `check-model.sh` then generates and runs the complete JP2/JPX model
under strict C11, ASan and UBSan.

These checks deliberately separate compiler responsibility from model
responsibility. ASN.1 constraints and ACN regions validate local shape and
lengths. `crossfield.c` handles relationships such as marker ordering, packet
coverage, JPX link rules, and distinctions between the standard and the served
profile. A future compiler feature such as a post-decoding validator could
invoke some of those rules automatically, but it would not make the rules
disappear or make generic `-atc` values representative of JPEG 2000 files.

## What the harness generates

`harness/` is self-contained: it does not consume asn1scc's own test values
(their identifiers vary between releases). It builds four canonical,
profile-valid files with the generated structs, derives every other vector
from them, and labels everything by decoding.

Bases: `jp2` (one codestream, default precincts), `jp2-precincts` (one
decomposition level with explicit precinct sizes, so the precinct rules have
something to mutate), `jpx-embedded` (`rreq`, two `jpch`, two `jp2c`), and
`jpx-linked` (`rreq`, two `jpch`, two `ftbl`/`flst`, one `dtbl` with two `url`
boxes). For the linked base the harness first writes the two referenced frames
(`jpx-linked-frame1.jp2`, `-frame2.jp2`), reads their codestream offsets
back, and puts them into the `flst` fragments, so the vector really
resolves; the manifest's `companions` column lists them.

The profile oracle also compares decoded fragment claims with the extents
measured from the generated companions. Four vectors shift the fragment start
or end by one byte, and one names a missing companion. Their `standard=valid`
label means that the JPX structure passes the standard model; it does not
certify the referenced byte range as a valid codestream. The cross-file
failure appears in the profile result as `flst.source-extent` or
`url.missing-companion`. The oracle resolves only the named corpus companions,
not arbitrary filesystem paths or network resources.

From each base:

1. **The base itself.**
2. **Field mutants** (`field_mutants[]` in `vectors.c`): one scalar field
   set to one interesting value — `min − 1`, `max + 1` at each layer's
   bound, and semantically loaded values (`xtsiz = 3` below `xsiz`,
   `progression = 3`). Names look like `jp2-siz.xosiz-1`.
3. **Rule mutants** (`rule_mutants[]`, `linked_rule_mutants[]`): one
   structural change per cross-field rule — a second COD or QCD, no QCD, a
   QCD after the tile-part, no PLT, contradictory TNsot, 65 tile-parts, a
   packet length beyond or short of the data, COD twice in a tile header or
   in a second tile-part, COD, QCD or COM in a tile header, COC or POC in
   the main header, 2:1 component sampling, a packet count above 2^31, no
   `jP` box, a wrong or unlisted `ftyp` brand, two `jp2c`, a `jp2c` inside a
   `jpch`, fewer `jp2c` than `jpch`, no `jpch`, `jp2c` and `ftbl` in one
   JPX, two `dtbl`, `DR = 0`, `NDR` mismatch, two `flst`, an `http` URL, a
   link to a `.jpx` — plus the valid shapes the rules must *not* reject: two
   tile-parts, TNsot given only in the second, 64 tile-parts, packet lengths
   split over two PLT segments, a COM in the main header, and `jp2c` boxes
   ahead of the `jpch` boxes. The deployed trailing-zero PLT form is generated
   as a deliberate `standard=invalid, profile=valid` case. Names look like
   `jp2-rule-codestream.no-plt-4` (the number is the mutant's index in its
   table). Append new rule mutants to preserve all existing fixture names;
   inserting, removing, or reordering entries renumbers later fixtures.
   Changes to the model or compiler can still change existing fixture bytes
   or labels and require separate review.
4. **Length mutants**: every `Lxxx`, `Psot` and `LBox` patched to `n − 1`,
   `n + 1`, and below its minimum. Names: `jp2-len-<offset>-<value>`.
5. **Code mutants**: each marker code is patched to `FF70` (undefined),
   `FF51` (SIZ out of place) and `FF90` (SOT where a segment was). Structured
   rule mutants cover box types without accidentally removing a different
   mandatory box: one appends an unknown `'abcd'` box, and another places a
   `jp2c` inside `jpch`. Names: `jp2-code-<offset>-<value>`.
6. **Signature mutant**: the `jP` box contents zeroed. Name: `jp2-sig-bad`.
7. **Region mutants**: file truncated by one byte (`-short`), one byte
   appended (`-long`), and for every box or segment its last payload byte
   removed with every enclosing length decremented (`-region-<offset>`), so
   the lengths stay consistent and only the innermost body comes up short.

Labels are never written by hand: `label()` decodes each vector with the
layer-1 decoder and the layer-2 decoder for its kind, runs the constraint
checkers and `crossfield.c`, and records the first failing reason in the
manifest's `reason` column.

Every mutant table entry does carry an *expectation* (`X_VALID`, `X_STD`
for standard-invalid, `X_PROF` for standard-valid but profile-invalid), and
`emit()` compares it with the label. This is not a second source of truth
for the corpus — the manifest always holds what the decoders said — it is a
self-check that the mutant did what its note claims. A setter that writes
the wrong field, a structural mutant that leaves the file valid, or a model
rule that quietly stopped firing shows up as a mismatch at generation time
rather than as an inexplicable server-test result later. Byte-level mutants
(length, code, region, signature) always expect standard-invalid; bases
always expect valid at both layers.

### Generated-API adaptation

`vectors.c` names generated symbols only through four macros at the top
(`ENC`, `DEC`, `VALID`, `INIT` → `<Type>_ACN_Encode` etc.) and one size
macro (`Jp2Family_REQUIRED_BYTES_FOR_ACN_ENCODING`); `mapping.h` names the
mapping-function prototypes through `MAPPING_ENCODE_NAME`/`_DECODE_NAME`;
`crossfield.h` includes the three generated headers by module name. Field
access assumes asn1scc's C conventions: `nCount`/`arr` for `SEQUENCE OF` and
`OCTET STRING`, `kind`/`u.<alt>` and `<Type>_<alt>_PRESENT` for `CHOICE`,
`exist.<field>` for `OPTIONAL`, a contained type appearing directly for
`OCTET STRING (CONTAINING X)`, and a NUL-terminated `char` array for
`IA5String`. If your release differs on any of these, the compiler will tell
you at exactly the access sites; nothing is hidden behind reflection.

Struct sizes: the generated `Jp2Family` is tens of MB (asn1scc allocates
every corpus bound inline); the harness allocates its few instances on the
heap. If your compiler reports larger sizes, lower the corpus bounds in the
`.asn1` files (they are marked) rather than the harness.

The reader/writer types in `jpeg2000-io.asn1` use the standard's bounds and
are decoded one at a time. Their sizes with asn1scc 4.9.3.0 on a 64-bit
target:

| Type | Bytes |
| --- | ---: |
| `PltSegment-Std` (65,532 `Iplt` entries) | 5,766,832 |
| `SizSegment-Std` (16,384 components) | 524,376 |
| `ComSegment-Std` (65,531 bytes) | 65,544 |
| `CodSegment-Std` | 616 |
| `QcdSegment-Std` (194 bytes) | 208 |
| `SotSegment` | 40 |
| `BoxHeader` | 32 |

The harness is the one place that depends on the generated API, so it is the
one thing to touch when regenerating with a newer asn1scc. The server and
`jpeg2000_test` never see generated code.

### Mapping functions

`harness/mapping.c` provides three length mappings in both directions.
ACN's length determinants count payload bytes, while the wire length fields
also count themselves and their neighbours:

| Name | Where | Encode (model → wire) | Decode (wire → model) |
| --- | --- | --- | --- |
| `lxxx` | every marker segment | `n + 2` | `n − 2` |
| `psot` | SOT | `n + 12` | `n − 12` |
| `lbox` | every box | `n + 8` | `n − 8` |

`boxtype` is decode-only. It preserves every box type listed in the ACN
choices and maps every other 32-bit TBox value to `'abcd'`, which selects the
opaque `other` alternative. The model uses the normalized type for validity
checks; the server retains the original bytes. The model encoder writes
`'abcd'` for `other`, so the corpus generator patches encoded TBox values to
exercise other unknown types. `check-model.sh` verifies that the mapping's
known-type list matches the ACN choices.

Before decoding, the corpus harness checks physical LBox boundaries. The
generated `CONTAINING` decoder uses LBox as a temporary stream size, which
can otherwise read beyond the buffer for malformed lengths. The check
follows the standard and profile's different superbox interpretations.

## The test loop in `jpeg2000_test.cc`

`CheckGeneratedCorpus()` reads every row of
`tests/vectors/j2k/manifest.tsv`. Corpus files remain in their committed
directory, so linked-JPX companions resolve under the same names used when
the harness generated them.

For each row, it compares the `profile` label with
`IndexGeneratedVector()`, which opens the file and attempts to index every
packet in every codestream. The loop collects profile mismatches, recording
whether each vector was unexpectedly accepted or where it was rejected, along
with the manifest's `reason` and `note` fields. After the loop, it prints
the collected mismatches and fails if any occurred. The `field` column
selects extra PLT-boundary and association-metadata checks for the applicable
rows.

The lazy packet lookup matters: some malformed packet-length tables are
detectable only when the packet index is built, not while the file structure is
opened.

The hand-built fixtures in `jpeg2000_test.cc` stay. Those that cover a
gap in the model ("Gaps") are the only test of that behaviour; the rest
overlap the corpus but run without it.

## Gaps (server accepts, model does not cover)

The corpus never generates these, so they are not failures, but the model is
narrower than the parser here and a hand-written test should keep covering
each until the model does:

- `Psot = 0` (tile-part to EOC) and `LBox = 0` (box to end of file): a
  region cannot be both determinant-sized and deduced.
- `LBox = 1` with `XLBox`: two possible determinants for one payload.
- Reader Requirements contents are opaque. Layer 1 checks the mandatory count
  and position, and the harness writes accurate base-box contents, but it does
  not generate field-level `rreq` mutants because the server ignores them.
- Marker codes outside the listed set are rejected at layer 1 only; layer 2
  has an `other` alternative and skips them exactly as the server does. A
  vector with e.g. a `CAP` segment is `standard=invalid, profile=valid` until
  the layer-1 marker list is extended with the applicable standard citation.
  Unknown box types are valid at both layers and are decoded as opaque boxes.
- `Rsiz` is modelled as a 16-bit capability field because the server preserves
  it for the client and validates packet-layout features separately. The
  standard model checks one level of `asoc` children and requires at least two;
  the profile treats its contents as opaque metadata, like the server.
- Deeper association trees and Multiple Codestream (`j2cx`) storage are
  exercised by explicit server fixtures, not generated-model labels.

`CheckSourceForms` covers 27 cases outside or alongside the ACN model:
normal, zero, and extended box lengths in JP2 and JPX; short, oversized, and
truncated XLBox headers; nested box limits; final zero-length parent and child
boxes; `Psot=0` with complete, missing, or misplaced EOC and incorrect PLT
coverage; nested opaque associations; and excluded `j2cx` codestream storage.
Accepted files are indexed through every declared packet. These tests do not
claim that the generated decoder supports those alternate encodings.

## Regeneration policy

Regenerate when a model or harness changes, or when moving to a newer asn1scc:

1. update `spec/VERSION` when the upstream compiler revision changes and run
   its regressions;
2. rerun Quick start step 2;
3. diff `manifest.tsv` against the previous one — new or removed rows must be
   explainable by the model, harness, or compiler change; label flips are
   findings;
4. commit the changed inputs, `VERSION`, and corpus together.

Never edit vector files or manifest labels by hand.

## Scope and limits

Covered: every marker the server reads, the full codestream framing, and the
JP2/JPX box structure including the linked-JPX form that is the production
workload. Not covered, because ACN cannot describe them: the JPIP request
syntax (text), and the JPP response stream (its message headers are
7-bit-group chains like `Iplt`, but the payload is sized by the *value*
assembled from the chain, which no determinant can reference).

The complementary live-server suite in `tests/server_test.cc` includes an
independent JPP reader in `tests/jpp_validation.h`. It reconstructs data-bins
across HTTP chunks and successive responses, then compares them byte-for-byte
with independently assembled source payloads and metadata placeholders.
It covers embedded and linked JPX with unequal codestream geometry, partial
and complete cache models, association metadata, cropped and changing windows,
`stream` and `context` selections, small `len` budgets, and gzip. Stateful runs
check reconnection, pipelining, browser-style socket reuse, channel-cache
isolation, and target-ID mismatches. The reader also has inheritance and
malformed-message self-tests. See `COVERAGE.md` for the complete matrix. Run it with:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R '^server$'
```

These synthetic packet payloads test transport, not image decoding. A real
JHV movie remains the decoding and rendering check. `ESAJPIP_SANITIZE` can
also be enabled for the live-server suite.

## Frequently asked

**Why not just write the test files by hand?** `jpeg2000_test.cc` already
contains a focused collection of hand-built cases. The corpus adds hundreds of
systematic cases, one per field, bound, and layer, with expected
outcomes derived rather than guessed. When the standard and the profile are
both written down formally, "what should the server do with this?" stops being
a judgement call.

**Why two layers?** So a failure says *which* rule the parser disagrees
with. "Rejects a valid standard file" and "accepts something the profile
excludes" are different bugs with different fixes, and layer 1 rejections
cite the standard's table so the disagreement can be settled by reading it.

**Why is asn1scc not in the build?** The generated code is only a judge, used
once per model change to label files. The server never links it; the tests read
the labelled files. Keeping the compiler offline avoids adding asn1scc and its
generated code to the production build.

**Why does the model have "corpus bounds"?** asn1scc's C structs embed
every list at its maximum size. Bounds like "65,535 boxes" would make a
single struct gigabytes large. The bounds only limit what the harness
generates; they are not claims about the standard.

**Can the generated decoder replace `file_manager.cc`?** No, and it is not
meant to. The server indexes multi-megabyte files by offset without
copying, parses PLT entries lazily, and reads text (JPIP requests); ACN
models fully-decoded, bounded records. The model's value is as a
specification and a test oracle.
