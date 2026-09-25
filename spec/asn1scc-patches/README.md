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
