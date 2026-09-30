# Retired ASN1SCC patches for 161cc246

These eleven patches apply, in the order listed in [`series`](series), to
unmodified upstream commit `161cc2465b568685c09b0a218149fb514ea2a95e`
(version 4.9.3.0). They are retained unchanged as historical reference and
are not applied by the current compiler build.

Upstream integrated the series in
[1e10e1b1](https://github.com/esa/asn1scc/commit/1e10e1b1da5ab9d0e864ae582665b8e5507a8e00)
and added corrections through
[1afedf0c](https://github.com/esa/asn1scc/commit/1afedf0c205c1fb783245e1f2d1e06cacc06d042)
(version 4.9.7.0), resolving
[issue #417](https://github.com/esa/asn1scc/issues/417).
The error-code portion of 0007 was already upstream in `d6fc8618`.
The published 0008 also required a subsequent error-code declaration fix
for length-embedded CONTAINING regions (`09ba658b`).

The resolved issue reports and local reproducers were removed after retirement.
See [`VALIDATION.md`](VALIDATION.md) for retirement verification.

Retired patches, in original `series` order:

- `0001-deferred-determinant-uninit.patch`: with `--acn-v2`, the C decoder copied
  a deferred determinant's temporary even when decoding it failed, reading an
  uninitialized value at end of stream. The three C copy templates in
  `StgC/acn_c.stg` now copy only when `ret` is true; a `-fsanitize=bool`
  check on test case 001 is added to `v4Tests/scripts/runWireTests.sh`.
- `0002-deferred-sequence-of-arguments.patch`: with `--acn-v2`, a determinant
  passed as an argument to the elements of a SEQUENCE OF child was not
  deferred: the encoder computed it from `arr[i1]` outside the element loop,
  with `i1` uninitialized. The two collectors of deferred determinants in
  `BackendAst/DAstACNDeferred.fs` now look through SEQUENCE OF to the
  element's reference type. Test case `25-ACNV2-BOUNDARIES/016` and its
  wire test in `v4Tests/scripts/runWireTests.sh`.
- `0003-deferred-sibling-consumers.patch`: a deferred determinant that a sibling
  also consumes (`head [size len]` beside `payload <len> []`): the decoder
  now keeps the ordinary variable the sibling reads (it did not compile),
  and the encoder patches the determinant from the sibling too, or fails on
  a mismatch (it wrote a wrong encoding). Test cases `017` and `018` (the
  JPEG 2000 Reader Requirements box, which needs both patches) and their
  wire tests.
- `0004-icdpdus-reference-init.patch`: with `-icdPdus`, the init function of a
  referenced type that is not complex (an OCTET STRING type assignment, say)
  was dropped although a PDU's init function calls it, so the generated C
  did not link. `BackendAst/DAstInitialize.fs` now records that call for
  every reference, as it did for complex types. Test case
  `v4Tests/test-cases/icd-pdus/001` and `v4Tests/scripts/runIcdPdusTests.sh`,
  which `../../../build-asn1scc.sh` runs. `jpeg2000/generate.sh` relies on it (the
  model's `RreqMask` and `Extra`).
- `0005-deferred-fixed-size-determinant.patch`: with `--acn-v2`, deferred size
  determinants for fixed-size OCTET STRING values were generated from a
  nonexistent C `nCount` member. `BackendAst/DAstACNDeferred.fs` now uses the
  declared ACN size when its minimum and maximum are equal, matching the
  existing nondeferred size-determinant rule. Variable-size values keep the
  runtime size expression. Test case `25-ACNV2-BOUNDARIES/019` compiles the
  generated C and checks its exact wire bytes and round trip in
  `v4Tests/scripts/runWireTests.sh`.
- `0006-containing-subtype-constraints.patch`: the validator of an
  `OCTET STRING (CONTAINING Subtype)` called the base type's validator when
  `Subtype` was a constrained reference type. The wrapper now calls the
  subtype validator, so validation and decoding enforce its constraints.
  Generated C changes only at the three affected profile checks in this
  model. Test case `25-ACNV2-BOUNDARIES/020` checks both rejection and an
  accepted value.
- `0007-deferred-patch-epilogue.patch`: with `--acn-v2`, an absent OPTIONAL
  producer was patched at the zero-initialized bitstream position, corrupting
  the first byte. The generator now patches only encoded producers and rejects
  a present direct consumer whose producer is absent. Deferred
  C/Rust patch templates now set the error code only on failure, backporting
  [upstream commit d6fc8618](https://github.com/esa/asn1scc/commit/d6fc8618).
  Test case `25-ACNV2-BOUNDARIES/021` checks exact wire bytes, successful
  error codes, and the inconsistent-presence failure.
- `0008-containing-length-overrun.patch`: a malformed length determinant could
  extend a deferred `OCTET STRING (CONTAINING ...)` region beyond the input
  buffer. The C decoder then read beyond the buffer; the analogous deferred
  `BIT STRING (CONTAINING ...)` and Rust paths also trusted the extended
  limit. All six C and six Rust region decoders now check the enclosing stream
  before narrowing it, leave the stream limit unchanged on rejection, and
  return the field's error code; the Rust stream checks are methods of
  `BitStream`, the stream the generated decoders are given. Test case
  `25-ACNV2-BOUNDARIES/022` checks both string kinds with valid and truncated
  inputs under AddressSanitizer.
- `0009-varsize-length-failure.patch`: the uPER decoder of a variable-size
  OCTET STRING, which ACN also uses, decoded the contents when the length
  failed to decode, and so accepted a length above the size bound, or no
  input, as an empty string; with no input it copied from the stream's null
  buffer. The C and Rust templates in `uper_c.stg` and `uper_rust.stg` now
  decode the contents only after the length, and a failed OCTET STRING or
  BIT STRING decode sets the error code. Test case `25-ACNV2-BOUNDARIES/023`
  and its wire test, built with UBSan.
- `0010-fixedsize-error-code.patch`: fixed-size OCTET STRING decoders
  rejected truncated input without setting an error code. Pass the allocated
  error code through the uPER and ACN generators and assign it on failure in
  C, Rust, and Ada. Other backend signatures follow the shared interface.
  Case `25-ACNV2-BOUNDARIES/024` checks all truncated byte lengths and complete
  values, standalone and nested.
- `0011-external-length-error-code.patch`: external OCTET STRING size
  rejection left error code zero. Move the C and Rust error assignments
  after the guarded decode, covering bounds and contents failures in both
  variable-size and fixed-size templates. Case `25-ACNV2-BOUNDARIES/025`
  covers rejected determinants, truncated contents, and valid boundaries.

