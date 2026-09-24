# ASN1SCC patches

These patches fix deferred ACN (`--acn-v2`) generation in ASN1SCC. They are
kept here both as a reproducible compiler input and as a reviewable series that
can be submitted upstream. The series applies to the exact unmodified revision
in [`../VERSION`](../VERSION).

Each patch contains one fix and the regression that demonstrates it:

1. `0001` passes determinants produced by referenced types across the reference
   boundary. It also fixes the C local initialization and explicit one-bit
   Boolean handling needed by that path.
2. `0002` prevents name collisions between ordinary and `CONTAINING` deferred
   specializations.
3. `0003` preserves bounded `CONTAINING` regions and mapping functions through
   parameterized choices. Its mapped-length regression checks the exact wire
   bytes as well as the generated round trip.

The regressions live in `v4Tests/test-cases/acn-v2`, outside the legacy ACN
test-case discovery tree, and are invoked by the C CI group. The compiler-side
changes update every affected backend interface. The executable regressions
currently exercise generated C, which is the backend used by esajpip.

[`../build-asn1scc.sh`](../build-asn1scc.sh) exports the pinned revision from a
local ASN1SCC repository, applies the series to that clean tree, builds it in
Docker, and runs the focused regressions. It never modifies the supplied
repository or depends on its working-tree state.

## Verification

The complete stack is checked in three layers:

- `build-asn1scc.sh` builds the ASN1SCC solution in the Linux compiler image;
- the same script generates all four focused regressions, compiles them as
  strict C11, runs their generated tests, and checks the mapped-length wire
  value; and
- esajpip's complete JP2/JPX model generates, compiles as strict C11, and runs
  its corpus harness under AddressSanitizer and UndefinedBehaviorSanitizer
  without label mismatches.

The complete ASN1SCC solution build checks that all backend interfaces remain
consistent. Runtime behavior is proven for C only. Upstream review may still
request equivalent executable coverage for another backend, but that is not
claimed by this series.
