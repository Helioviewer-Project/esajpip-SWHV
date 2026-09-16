# JPEG 2000 file model and test-vector corpus

## Start here

esajpip reads JPEG 2000 files (`.jp2`, `.jpx`) and serves pieces of them to
JHelioviewer over JPIP. The code that reads those files is
`src/jpeg2000/file_manager.cc`. It has to reject malformed input without
crashing, and it has to accept exactly the files the project promises to
support (`JPIP_PROFILE.md`, "Supported JPEG 2000 sources").

This directory is a **formal description of what a valid file looks like**,
written in a machine-readable notation (ASN.1 with ACN encoding rules), plus
a small program that turns that description into a **corpus of test files**:
a few thousand tiny `.jp2`/`.jpx` files, each labelled "the server must
accept this" or "the server must reject this". `tests/jpeg2000_test.cc` then
opens every one and checks that the server agrees.

The point of doing it this way rather than writing test files by hand:

- The labels come from the description, not from a person. Every field of
  every marker segment and box gets a test at each edge of its allowed
  range, systematically, which nobody does by hand.
- The description exists in two layers — *what the standard allows* and
  *what esajpip supports* — so a test failure tells you which of the two
  the parser disagrees with, and cites the clause of the standard.
- The description is the specification. `JPIP_PROFILE.md` says in prose
  what the server accepts; `spec/*.asn1` says it in a form a test can prove.

Nothing here is part of the server build. The ASN.1 compiler runs offline,
on a developer machine, only when the description changes. The repository
consumes only the committed corpus, through plain CMake/CTest.

If you are here to **run the tests**, you need nothing from this directory:
the corpus is already in `tests/vectors/j2k/` and `ctest` uses it.
If you are here to **change what the server accepts**, read "Background",
then "Quick start", then edit the model and regenerate.
If a test **just failed**, go to "When a vector fails".

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
field, so you rarely need the standards themselves.

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
| **model** | The six `spec/*.asn1` + `*.acn` files together. |
| **layer 1 / standard** | Types with the ranges and rules of T.800/T.801. |
| **layer 2 / profile** | `*-Profile` types: layer 1 narrowed to what esajpip serves. |
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
| `VERSION` | The asn1scc release the corpus is generated with: `4.9.0.0`. |
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

Only needed when a model file changes. Everything runs offline.

1. Get asn1scc 4.9.0.0 and build it per its README:
   `git -C ~/git/asn1scc checkout 4.9.0.0`. `spec/VERSION` pins this.
2. Compile the model (checks the ASN.1 and ACN syntax; ~seconds):

   ```sh
   cd spec
   asn1scc -c -ACN --acn-v2 -o /tmp/j2k-gen \
       j2k-headers.asn1 j2k-headers.acn \
       j2k-codestream.asn1 j2k-codestream.acn \
       jp2-boxes.asn1 jp2-boxes.acn
   ```

   You should get one `.c`/`.h` per module plus `asn1crt*.{c,h}` in
   `/tmp/j2k-gen`. Hyphens become underscores (`Jp2File-Profile` →
   `Jp2File_Profile`). First time: read "Compiler checks" below.
3. Run the compiler's own round-trip test (encodes and decodes every type at
   its bounds; proves the `.acn` describes the bytes consistently):

   ```sh
   asn1scc -c -ACN --acn-v2 -atc -o /tmp/j2k-gen \
       j2k-headers.asn1 j2k-headers.acn \
       j2k-codestream.asn1 j2k-codestream.acn \
       jp2-boxes.asn1 jp2-boxes.acn
   cd /tmp/j2k-gen && cc -O1 -g -fsanitize=address,undefined *.c ../spec/harness/mapping.c -o atc && ./atc
   ```
4. Build the harness against the generated code and run it:

   ```sh
   cd spec/harness
   GEN=$(ls /tmp/j2k-gen/[a-z]*.c | grep -v -e mainprogram -e test_case -e auto_tcs)
   cc -O1 -g -fsanitize=address,undefined -I/tmp/j2k-gen \
      vectors.c crossfield.c mapping.c $GEN -o vectors
   mkdir -p ../../tests/vectors/j2k && ./vectors ../../tests/vectors/j2k
   ```

   It prints `vectors: N vectors written … (M valid at both layers)`.
   Expect N in the low thousands and M a few dozen.
5. Run the server tests and read "When a vector fails" for anything red:

   ```sh
   cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build && ctest --test-dir build
   ```
6. Commit the model change, `spec/VERSION` if it changed, and
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
  generates one.
- **Layer 2 can only narrow.** A `*-Profile` type is either a `WITH
  COMPONENTS` subtype (ranges narrowed) or, where the framing has to repeat
  with profile bodies inside `CONTAINING`, a copy with profile types
  substituted. It cannot *widen*. So if esajpip accepts something T.800
  forbids, that cannot be written into layer 2; it shows up as a failing
  test instead ("Expected initial failures").

## The test contract

For every vector, `jpeg2000_test` writes it (and its companions) to the
image directory under the names in its manifest row, calls
`FileManager::OpenImage`, and — if that succeeds — indexes the first packet
of codestream 0 with `GetPacket(file, 0, jpeg2000::Packet(0, 0, 0, jpeg2000::Point()), &segment)`
(the call `jpeg2000_test.cc` already makes for its hand-built files).
"Accept" means both succeed; "reject" means either fails. The second step
matters because the server parses PLT entries lazily: a malformed or
over-long packet length is only detected when a packet is indexed.

| Standard | Profile | The server must |
| --- | --- | --- |
| valid | valid | accept |
| valid | invalid | reject, for the profile reason |
| invalid | (invalid) | reject |

A vector is *valid at a layer* when all three hold: the generated ACN decoder
accepts it (this is what catches region overruns, leftover bytes, wrong
`CHOICE` selection, and unknown codes), the generated constraint checker for
that layer accepts the decoded value (`<Type>_IsConstraintValid`), and the
cross-field rules for that layer hold (`crossfield.c`). Only the third part
is hand-written, and it transcribes a short list that cites its sources.

The corpus tests *acceptance* (open plus first-packet indexing), not
serving. Packet data in generated files is arbitrary bytes; nothing here
claims a file decodes to an image.

Manifest columns: `file kind standard profile field note companions`.
`field` names the mutated field or rule (or `-` for a base); `note` is the
mutant's intent in words; `companions` lists files that must sit next to
the vector (the `.jp2` frames a linked JPX points at).

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
- **`standard=invalid`, server accepted.** The parser accepts something the
  standard forbids. See "Expected initial failures" — it may already be
  listed with a suggested fix. Otherwise decide: tighten the parser, or
  document the leniency in `JPIP_PROFILE.md` and relax the rule in the
  model (a layer-1 rule can only be relaxed by widening the base type).
- **`standard=invalid profile=valid`.** Only possible for unknown marker
  codes or box types: layer 2 skips them as the server does, layer 1
  rejects them because they are not in its list. If a real file carries
  such a code, add it to the layer-1 set with its citation.

The `field` column also tells you which of the harness's mutation classes
produced the vector (see "What the harness generates"), which narrows down
the parser code involved: a length mutant points at `ReadBoxHeader` /
`ReadCodestream`'s limit checks, a field mutant at the corresponding
`Read*Marker`, a rule mutant at the structural checks.

## Expected initial failures

Places where the server is *more lenient* than T.800. A subtype cannot widen
its base, so each needs a decision: tighten the parser, or relax the model
and document the leniency in `JPIP_PROFILE.md`. Three earlier ones were
resolved in the parser (`e1b48dd` reserved code-block style bits, `63161e4`
zero precinct exponents above r = 0, `c309867` the `jP`/`ftyp` preamble);
the corpus expects rejection for those, matching the hand-written tests
added with each fix.

The remaining ones are all in `ReadCodestream`'s marker loop, which
dispatches on the marker code with no notion of "main header has ended":

1. **A second COD (or QCD) in the main header.** Vector
   `jp2-rule-codestream.one-cod-before-sot-0`. T.800 A.6.1: COD appears once
   in the main header. `ReadCODMarker` runs again and overwrites the
   parameters. Fix: `if (cod) return false;` before the call (mirror `siz`).
2. **A marker segment between tile-parts.** Vector
   `jp2-rule-codestream.segment-after-sot-2` (QCD after the first tile-part's
   data). T.800 A.3: after the first SOT only SOT and EOC may follow a
   tile-part. The loop accepts any segment there and skips or re-parses it.
   Fix: once a SOD has been passed, reject every code except SOT and EOC.
3. **TNsot inconsistent with the tile-part count.** Vector
   `jp2-sot.tnsot-2` (one tile-part, TNsot = 2). A.4.2: a non-zero TNsot is
   the number of tile-parts of the tile. `ReadSOTMarker` checks only
   `tpsot < tnsot` per segment. Fix: remember the first non-zero TNsot and,
   at EOC, require `packets.size()` to equal it; also require TPsot to run
   0, 1, 2, … (the model checks the sequence too; today a codestream whose
   first tile-part has TPsot = 1 with TNsot = 0 would be accepted).

None of the three affects serving of well-formed files; all three are a few
lines in `file_manager.cc`.

A JPX carrying both `jp2c` and `ftbl` boxes is *not* a failure: links take
precedence and embedded codestreams are ignored (`JPIP_PROFILE.md`), and the
model's `jpx.linked-precedence` vector is expected to be accepted. Likewise
an `Iplt` written in more 7-bit groups than its value needs is valid at both
layers: the server bounds packet lengths by value, not by byte count.

Any other failure is a finding about the parser or the model; read the
clause cited on the model field before changing either.

## Compiler checks (first run of asn1scc on this model)

The model was written against the ACN grammar of asn1scc 4.9.0.0
(`Antlr/acn.g`) and its release notes, but has not been compiled with it.
Settled by the grammar: dotted field paths in `size`, parameters and
`present-when`; `present-when` boolean expressions (`not`, `and`, `or`,
`!=`), which the layer-2 `other` catch-alls use; CHOICE alternatives
selected by a parameter; `termination-pattern` with an octet-string
literal; the bare-field boolean form `[present-when customPrecincts]`;
`size deduced` (implemented since 4.8.0.0 for `SEQUENCE OF`, `OCTET STRING`
and `IA5String` — ignore the "draft" header on `Docs/deduced-size-spec.md`);
`--acn-v2`, the single-pass encoder that makes inserted lengths work.

Three spellings to confirm when the compiler first runs; each has a
fallback that changes no bytes:

1. **`size null-terminated` on a `SEQUENCE OF`** (`Codestream.segments`,
   `TilePartRest.headers`, terminated by EOC / SOD). If only strings accept
   it, wrap those lists in a `CONTAINING` region with `size deduced` and add
   EOC/SOD as explicit trailing fields.
2. **`mapping-function` prototypes.** Check the generated header for the C
   signatures it expects and adjust `MAPPING_ENCODE_NAME` /
   `MAPPING_DECODE_NAME` in `harness/mapping.h`.
3. **A parameter passed through a `CONTAINING` field**
   (`payload <tbox> [size lbox]` in `jp2-boxes.acn`). If rejected, move
   `tbox` into each wrapper as an ACN-inserted determinant.

Optional, later: 4.9.0.0 has `post-decoding-validator <name>`, which makes
the generated decoder call a C function after decoding. Attaching the
`crossfield.c` rules that way would fold the third part of the validity
rule into the decoder. Start without it; a validator failure inside `-atc`
aborts the round-trip test rather than labelling a vector.

## What the harness generates

`harness/` is self-contained: it does not consume asn1scc's own test values
(their identifiers vary between releases). It builds four canonical,
profile-valid files with the generated structs, derives every other vector
from them, and labels everything by decoding.

Bases: `jp2` (one codestream, default precincts), `jp2-precincts` (one
decomposition level with explicit precinct sizes, so the precinct rules have
something to mutate), `jpx-embedded` (two `jpch`, two `jp2c`), `jpx-linked`
(two `jpch`, two `ftbl`/`flst`, one `dtbl` with two `url` boxes). For the
linked base the harness first writes the two referenced frames
(`jpx-linked-frame1.jp2`, `-frame2.jp2`), reads their codestream offsets
back, and puts them into the `flst` fragments, so the vector really
resolves; the manifest's `companions` column lists them.

From each base:

1. **The base itself.**
2. **Field mutants** (`field_mutants[]` in `vectors.c`): one scalar field
   set to one interesting value — `min − 1`, `max + 1` at each layer's
   bound, and semantically loaded values (`xtsiz = 3` below `xsiz`,
   `progression = 3`). Names look like `jp2-siz.xosiz-1`.
3. **Rule mutants** (`rule_mutants[]`, `linked_rule_mutants[]`): one
   structural change per cross-field rule — a second COD, no QCD, a QCD
   after the tile-part, no PLT, 65 tile-parts, a packet length beyond the
   data, no `jP` box, two `jp2c`, `DR = 0`, `NDR` mismatch, two `flst`, an
   `http` URL. Names look like `jp2-rule-codestream.no-plt-3`.
4. **Length mutants**: every `Lxxx`, `Psot` and `LBox` patched to `n − 1`,
   `n + 1`, and below its minimum. Names: `jp2-len-<offset>-<value>`.
5. **Code mutants**: each marker code patched to `FF70` (undefined), `FF51`
   (SIZ out of place) and `FF90` (SOT where a segment was); each box type to
   `'abcd'` and to `'jp2c'`. Names: `jp2-code-<offset>-<value>`.
6. **Signature mutant**: the `jP` box contents zeroed. Name: `jp2-sig-bad`.
7. **Region mutants**: file truncated by one byte (`-short`), one byte
   appended (`-long`), and for every box or segment its last payload byte
   removed with every enclosing length decremented (`-region-<offset>`), so
   the lengths stay consistent and only the innermost body comes up short.

Labels are never written by hand: `label()` decodes each vector with the
layer-1 decoder and the layer-2 decoder for its kind, runs the constraint
checkers and `crossfield.c`, and records the first failing reason.

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

The harness is the one place that depends on the generated API, so it is the
one thing to touch when regenerating with a newer asn1scc. The server and
`jpeg2000_test` never see generated code.

### Mapping functions

Three, in `harness/mapping.c`, each in both directions, because ACN's
length determinants count payload bytes while the standard's length fields
also count themselves and their neighbours:

| Name | Where | Encode (model → wire) | Decode (wire → model) |
| --- | --- | --- | --- |
| `lxxx` | every marker segment | `n + 2` | `n − 2` |
| `psot` | SOT | `n + 12` | `n − 12` |
| `lbox` | every box | `n + 8` | `n − 8` |

## The test loop in `jpeg2000_test.cc`

If `tests/vectors/j2k/manifest.tsv` exists, for each row: copy the file and
its companions into the test image directory under their names, then:

```cpp
bool expect = row.standard == "valid" && row.profile == "valid";
jpeg2000::FileManager manager;
bool accepted = OpenImage(directory, row.file, &manager);
if (accepted) {
    data::File *file = manager.GetFile(manager.GetImage()->GetPathName(0));
    data::FileSegment segment;
    accepted = file != NULL && manager.GetImage()->GetPacket(
            file, 0, jpeg2000::Packet(0, 0, 0, jpeg2000::Point()), &segment);
}
Check(accepted == expect, ("vector " + row.file + ": expected " +
                           (expect ? "accept" : "reject")).c_str());
```

For rows that are `standard=valid, profile=invalid`, also assert that the
rejection's log line names the profile rule (origin, tile index, dimension
limit, PLT missing, tile-part count, link structure). The trace test helpers
already capture log output.

Once the corpus is in, the hand-built `MakeCodestream`, `MakeJP2`,
`MakeEmbeddedJPX` and `MakeLinkedJPX` in `jpeg2000_test.cc` are redundant
except for the linked-file resolution case ("Gaps"); retire the rest.

## Gaps (server accepts, model does not cover)

The corpus never generates these, so they are not failures, but the model is
narrower than the parser here and a hand-written test should keep covering
each until the model does:

- `Psot = 0` (tile-part to EOC) and `LBox = 0` (box to end of file): a
  region cannot be both determinant-sized and deduced.
- `LBox = 1` with `XLBox`: two possible determinants for one payload.
- Marker codes and box types outside the listed sets are rejected at
  layer 1 only; layer 2 has an `other` alternative and skips them exactly as
  the server does. A vector with e.g. a `CAP` segment would be
  `standard=invalid, profile=valid` — a finding about layer 1's list, not a
  gap.
- `Rsiz` is modelled as `0..65535` although Table A.10 defines 0–2, on
  purpose (deployed files carry amendment bits); `asoc` nesting is limited
  to one level.
- The linked-JPX check that each `flst` fragment equals the referenced
  file's codestream extent needs the second file; keep `MakeLinkedJPX`'s
  test for it.

## Regeneration policy

Regenerate only when a model file changes or when moving to a newer asn1scc:

1. update `spec/VERSION`;
2. rerun Quick start steps 2–4;
3. diff `manifest.tsv` against the previous one — new or removed rows must be
   explainable by the model change; label flips are findings;
4. commit model, `VERSION`, and corpus in one change.

Never edit vector files or manifest labels by hand.

## Scope and limits

Covered: every marker the server reads, the full codestream framing, and the
JP2/JPX box structure including the linked-JPX form that is the production
workload. Not covered, because ACN cannot describe them: the JPIP request
syntax (text), and the JPP response stream (its message headers are
7-bit-group chains like `Iplt`, but the payload is sized by the *value*
assembled from the chain, which no determinant can reference). For those the
complementary tool is a fuzz target over `Request::Parse` and the channel's
request loop using the existing `ESAJPIP_SANITIZE` build option.

## Frequently asked

**Why not just write the test files by hand?** `jpeg2000_test.cc` does,
for about twenty cases. The corpus has a few thousand, one per field per
bound per layer, and their expected outcomes are derived rather than
guessed. When the standard and the profile are both written down formally,
"what should the server do with this?" stops being a judgement call.

**Why two layers?** So a failure says *which* rule the parser disagrees
with. "Rejects a valid standard file" and "accepts something the profile
excludes" are different bugs with different fixes, and layer 1 rejections
cite the standard's table so the disagreement can be settled by reading it.

**Why is asn1scc not in the build?** The generated code is only a judge,
used once per model change to label files. The server never links it; the
tests read the labelled files. Keeping the compiler offline keeps the
build's dependency list (GLib, zlib) unchanged.

**Why does the model have "corpus bounds"?** asn1scc's C structs embed
every list at its maximum size. Bounds like "65,535 boxes" would make a
single struct gigabytes large. The bounds only limit what the harness
generates; they are not claims about the standard.

**Can the generated decoder replace `file_manager.cc`?** No, and it is not
meant to. The server indexes multi-megabyte files by offset without
copying, parses PLT entries lazily, and reads text (JPIP requests); ACN
models fully-decoded, bounded records. The model's value is as a
specification and a test oracle.
