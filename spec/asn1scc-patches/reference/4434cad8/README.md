# Retired ASN1SCC patches for 4434cad8

These three patches apply, in numbered order, to unmodified upstream commit
`4434cad8bbcc436183ce4cc15721392be1466e36`. They are retained unchanged as
historical reference and are not applied by the current compiler build.

They are the former `0001` to `0003` fixes for deferred ACN (`--acn-v2`)
generation. The pinned compiler includes the upstream fix for
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
