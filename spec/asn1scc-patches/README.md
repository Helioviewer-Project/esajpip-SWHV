# ASN1SCC patches

[`../build-asn1scc.sh`](../build-asn1scc.sh) applies the patches listed in
[`series`](series), in that order, to the upstream revision pinned in
[`../VERSION`](../VERSION) before building the compiler. Each one fixes a
bug still present in that pinned upstream revision. When updating `VERSION`,
retest each bug against the new revision and remove any patch it supersedes.
The numbered filenames show the application order directly. Keep retired
series in separate `reference/<upstream-base>/` directories so their numbers
do not mix with those of another series.
Each active patch has a matching numbered directory in
[`../asn1scc-issues/`](../asn1scc-issues/) with its report and reproducer.

The build also runs the standalone C reproducers for 0008 (ASan) and 0009
(UBSan, both uPER and ACN) against the installed compiler. The 0009 Rust
reproducer is separate: run `run_rust.sh` with Cargo and the patched compiler.

Applied now, in `series` order:

- `0001-deferred-determinant-uninit.patch`: with `--acn-v2`, the C decoder copied
  a deferred determinant's temporary even when decoding it failed, reading an
  uninitialized value at end of stream. The three C copy templates in
  `StgC/acn_c.stg` now copy only when `ret` is true; a `-fsanitize=bool`
  check on test case 001 is added to `v4Tests/scripts/runWireTests.sh`.
  Report and reproducer:
  [`../asn1scc-issues/0001-deferred-determinant-uninit/`](../asn1scc-issues/0001-deferred-determinant-uninit/).
- `0002-deferred-sequence-of-arguments.patch`: with `--acn-v2`, a determinant
  passed as an argument to the elements of a SEQUENCE OF child was not
  deferred: the encoder computed it from `arr[i1]` outside the element loop,
  with `i1` uninitialized. The two collectors of deferred determinants in
  `BackendAst/DAstACNDeferred.fs` now look through SEQUENCE OF to the
  element's reference type. Test case `25-ACNV2-BOUNDARIES/016` and its
  wire test in `v4Tests/scripts/runWireTests.sh`. Report and reproducer:
  [`../asn1scc-issues/0002-deferred-sequence-of-arguments/`](../asn1scc-issues/0002-deferred-sequence-of-arguments/).
- `0003-deferred-sibling-consumers.patch`: a deferred determinant that a sibling
  also consumes (`head [size len]` beside `payload <len> []`): the decoder
  now keeps the ordinary variable the sibling reads (it did not compile),
  and the encoder patches the determinant from the sibling too, or fails on
  a mismatch (it wrote a wrong encoding). Test cases `017` and `018` (the
  JPEG 2000 Reader Requirements box, which needs both patches) and their
  wire tests. Report and reproducer:
  [`../asn1scc-issues/0003-deferred-sibling-consumers/`](../asn1scc-issues/0003-deferred-sibling-consumers/).
- `0004-icdpdus-reference-init.patch`: with `-icdPdus`, the init function of a
  referenced type that is not complex (an OCTET STRING type assignment, say)
  was dropped although a PDU's init function calls it, so the generated C
  did not link. `BackendAst/DAstInitialize.fs` now records that call for
  every reference, as it did for complex types. Test case
  `v4Tests/test-cases/icd-pdus/001` and `v4Tests/scripts/runIcdPdusTests.sh`,
  which `../build-asn1scc.sh` runs. `lib/generate.sh` relies on it (the
  model's `RreqMask` and `Extra`). Report and reproducer:
  [`../asn1scc-issues/0004-icdpdus-reference-init/`](../asn1scc-issues/0004-icdpdus-reference-init/).
- `0005-deferred-fixed-size-determinant.patch`: with `--acn-v2`, deferred size
  determinants for fixed-size OCTET STRING values were generated from a
  nonexistent C `nCount` member. `BackendAst/DAstACNDeferred.fs` now uses the
  declared ACN size when its minimum and maximum are equal, matching the
  existing nondeferred size-determinant rule. Variable-size values keep the
  runtime size expression. Test case `25-ACNV2-BOUNDARIES/019` compiles the
  generated C and checks its exact wire bytes and round trip in
  `v4Tests/scripts/runWireTests.sh`. Report and reproducer:
  [`../asn1scc-issues/0005-deferred-fixed-size-determinant/`](../asn1scc-issues/0005-deferred-fixed-size-determinant/).
- `0006-containing-subtype-constraints.patch`: the validator of an
  `OCTET STRING (CONTAINING Subtype)` called the base type's validator when
  `Subtype` was a constrained reference type. The wrapper now calls the
  subtype validator, so validation and decoding enforce its constraints.
  Generated C changes only at the three affected profile checks in this
  model. Test case `25-ACNV2-BOUNDARIES/020` checks both rejection and an
  accepted value. Report and reproducer:
  [`../asn1scc-issues/0006-containing-subtype-constraints/`](../asn1scc-issues/0006-containing-subtype-constraints/).
- `0007-deferred-patch-epilogue.patch`: with `--acn-v2`, an absent OPTIONAL
  producer was patched at the zero-initialized bitstream position, corrupting
  the first byte. The generator now patches only encoded producers and rejects
  a present direct consumer whose producer is absent. Deferred
  C/Rust patch templates now set the error code only on failure, backporting
  [upstream commit d6fc8618](https://github.com/esa/asn1scc/commit/d6fc8618).
  Test case `25-ACNV2-BOUNDARIES/021` checks exact wire bytes, successful
  error codes, and the inconsistent-presence failure. Report and reproducer:
  [`../asn1scc-issues/0007-deferred-patch-epilogue/`](../asn1scc-issues/0007-deferred-patch-epilogue/).
- `0008-containing-length-overrun.patch`: a malformed length determinant could
  extend a deferred `OCTET STRING (CONTAINING ...)` region beyond the input
  buffer. The C decoder then read beyond the buffer; the analogous deferred
  `BIT STRING (CONTAINING ...)` and Rust paths also trusted the extended
  limit. All six C and six Rust region decoders now check the enclosing stream
  before narrowing it, leave the stream limit unchanged on rejection, and
  return the field's error code; the Rust stream checks are methods of
  `BitStream`, the stream the generated decoders are given. Test case
  `25-ACNV2-BOUNDARIES/022` checks both string kinds with valid and truncated
  inputs under AddressSanitizer. Report and reproducer:
  [`../asn1scc-issues/0008-containing-length-overrun/`](../asn1scc-issues/0008-containing-length-overrun/).
- `0009-varsize-length-failure.patch`: the uPER decoder of a variable-size
  OCTET STRING, which ACN also uses, decoded the contents when the length
  failed to decode, and so accepted a length above the size bound, or no
  input, as an empty string; with no input it copied from the stream's null
  buffer. The C and Rust templates in `uper_c.stg` and `uper_rust.stg` now
  decode the contents only after the length, and a failed OCTET STRING or
  BIT STRING decode sets the error code. Test case `25-ACNV2-BOUNDARIES/023`
  and its wire test, built with UBSan. Report and reproducer:
  [`../asn1scc-issues/0009-varsize-length-failure/`](../asn1scc-issues/0009-varsize-length-failure/).

## Reference: the former fixes for deferred ACN

The patches in [`reference/4434cad8/`](reference/4434cad8/) are not in
`series`. They are the former `0001` to `0003` fixes for deferred ACN
(`--acn-v2`) generation.
They apply, in that order, to the
unmodified ASN1SCC commit `4434cad8bbcc436183ce4cc15721392be1466e36`, not to
`VERSION`. `VERSION` now pins the upstream fix for
[issue #415](https://github.com/esa/asn1scc/issues/415), which replaces them.
They are retained as a reference. Revalidation on 2026-09-26 confirmed that
all three apply in order to an archive of `4434cad8`, and that
`sh v4Tests/scripts/runDeferredAcnRegressions.sh` passes after a complete
build of that patched archive. The unmodified pinned `VERSION` also passes
the former issue #415 cases in `25-ACNV2-BOUNDARIES`, without these patches.

Each contains one fix and the regression that demonstrates it:

1. `0001` passes determinants produced by referenced types across the reference
   boundary. It also fixes the C local initialization and explicit one-bit
   Boolean handling needed by that path.
2. `0002` prevents name collisions between ordinary and `CONTAINING` deferred
   specializations.
3. `0003` preserves bounded `CONTAINING` regions and mapping functions through
   parameterized choices. Its mapped-length regression checks the exact wire
   bytes as well as the generated round trip.

They add four focused C regressions under `v4Tests/test-cases/acn-v2/`.
