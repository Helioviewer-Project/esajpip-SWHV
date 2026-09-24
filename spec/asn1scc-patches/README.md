# ASN1SCC patches (reference)

These patches are the former local fix for deferred ACN (`--acn-v2`)
generation in ASN1SCC. They apply, in [`series`](series) order, to the exact
unmodified ASN1SCC commit
`4434cad8bbcc436183ce4cc15721392be1466e36`. All three patches still
apply to that commit.

They are retained as a reference. [`../VERSION`](../VERSION) now pins the
upstream compiler fix for [issue #415](https://github.com/esa/asn1scc/issues/415),
and [`../build-asn1scc.sh`](../build-asn1scc.sh) builds that upstream revision
without applying this series.

Each patch contains one fix and the regression that demonstrates it:

1. `0001` passes determinants produced by referenced types across the reference
   boundary. It also fixes the C local initialization and explicit one-bit
   Boolean handling needed by that path.
2. `0002` prevents name collisions between ordinary and `CONTAINING` deferred
   specializations.
3. `0003` preserves bounded `CONTAINING` regions and mapping functions through
   parameterized choices. Its mapped-length regression checks the exact wire
   bytes as well as the generated round trip.

The patches add four focused C regressions under
`v4Tests/test-cases/acn-v2/`. The former build workflow compiled them as
strict C11 and checked the mapped-length wire value. The current build runs
the upstream ACN v2 regressions and wire checks instead.
