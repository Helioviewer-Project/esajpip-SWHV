# ASN1SCC patch reference

No local patches are applied. [`../VERSION`](../VERSION) pins upstream
ASN1SCC 4.9.7.0 at `1afedf0c205c1fb783245e1f2d1e06cacc06d042`.
[`../build-asn1scc.sh`](../build-asn1scc.sh) builds that unmodified revision
and runs the upstream CONTAINING, deduced-size, ACN v2, wire and initializer
regressions, followed by all eleven preserved C reproducers.

The eleven fixes from [issue #417](https://github.com/esa/asn1scc/issues/417)
are retired under [`reference/161cc246/`](reference/161cc246/), separate from
the earlier series. That directory records their original base, numbered
application order, matching reports and retirement validation. The patches
and reproducers are retained so that future compiler revisions can be
checked against the original failures. Rust reproducers for 0009 through
0011 remain separate and require Cargo.

## Reference: the former fixes for deferred ACN

The patches in [`reference/4434cad8/`](reference/4434cad8/) are not applied. They are the former `0001` to `0003` fixes for deferred ACN
(`--acn-v2`) generation.
They apply, in that order, to the
unmodified ASN1SCC commit `4434cad8bbcc436183ce4cc15721392be1466e36`, not to
`VERSION`. The pinned compiler includes the upstream fix for
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
