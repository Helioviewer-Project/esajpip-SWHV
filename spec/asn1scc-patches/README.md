# ASN1SCC patches

[`../build-asn1scc.sh`](../build-asn1scc.sh) applies the patches listed in
[`series`](series), in that order, to the upstream revision pinned in
[`../VERSION`](../VERSION) before building the compiler. Each one is a local
fix that upstream does not have yet. Remove it from `series` (and the file)
when `VERSION` moves to an upstream revision that contains the fix.

Applied now:

- `deferred-determinant-uninit.patch`: with `--acn-v2`, the C decoder copied
  a deferred determinant's temporary even when decoding it failed, reading an
  uninitialized value at end of stream. The three C copy templates in
  `StgC/acn_c.stg` now copy only when `ret` is true; a `-fsanitize=bool`
  check on test case 001 is added to `v4Tests/scripts/runWireTests.sh`.
  Report and reproducer:
  [`../asn1scc-issues/deferred-determinant-uninit/`](../asn1scc-issues/deferred-determinant-uninit/).
- `deferred-sequence-of-arguments.patch`: with `--acn-v2`, a determinant
  passed as an argument to the elements of a SEQUENCE OF child was not
  deferred: the encoder computed it from `arr[i1]` outside the element loop,
  with `i1` uninitialized. The two collectors of deferred determinants in
  `BackendAst/DAstACNDeferred.fs` now look through SEQUENCE OF to the
  element's reference type. Test case `25-ACNV2-BOUNDARIES/016` and its
  wire test in `v4Tests/scripts/runWireTests.sh`.
- `deferred-sibling-consumers.patch`: a deferred determinant that a sibling
  also consumes (`head [size len]` beside `payload <len> []`): the decoder
  now keeps the ordinary variable the sibling reads (it did not compile),
  and the encoder patches the determinant from the sibling too, or fails on
  a mismatch (it wrote a wrong encoding). Test cases `017` and `018` (the
  JPEG 2000 Reader Requirements box, which needs both patches) and their
  wire tests.

  Report and reproducers for both:
  [`../asn1scc-issues/deferred-sequence-of-determinant/`](../asn1scc-issues/deferred-sequence-of-determinant/).

## Reference: the former fixes for deferred ACN

`0001` to `0003` are not in `series`. They are the former local fix for
deferred ACN (`--acn-v2`) generation and apply, in that order, to the
unmodified ASN1SCC commit `4434cad8bbcc436183ce4cc15721392be1466e36`, not to
`VERSION`. `VERSION` now pins the upstream fix for
[issue #415](https://github.com/esa/asn1scc/issues/415), which replaces them.
They are retained as a reference.

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
