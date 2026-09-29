# External OCTET STRING length rejection leaves error code zero

## Upstream status

Fixed upstream in ASN1SCC 4.9.7.0, pinned at
`1afedf0c205c1fb783245e1f2d1e06cacc06d042`, as part of
[issue #417](https://github.com/esa/asn1scc/issues/417).
The patch is retired under
[`../../asn1scc-patches/reference/161cc246/`](../../asn1scc-patches/reference/161cc246/).
The report below describes the original failure; the reproducer is retained
and passes with the unmodified pinned upstream compiler.

Base: asn1scc `161cc2465b568685c09b0a218149fb514ea2a95e`, after patches
0001 through 0010.

## Observed failure and cause

The repository fuzz harness reported `RreqHeader: decoded=0 error=0`.
`RreqHeader_fuam_ACN_Decode` initializes the error to zero and rejects a
mask-length determinant outside 1..8. Its error assignment is inside the
successful bounds-check branch, so rejection leaves zero. A one-byte
encoding `00` reaches this path; the corresponding `dcm` helper has the
same omission.

The C and Rust `oct_external_field_decode` templates both omit the error
on determinant rejection. This affects the lower/upper-bound branch and
the upper-bound-only branch. The fixed-size external-determinant template
has the same placement. Ada already assigns the error at the bounds check.

The Boolean failure is correct. This patch makes the failure diagnostic
consistent with a truncated contents decode. It does not change which
inputs are accepted. No universal documented guarantee that every failed
decode sets a nonzero error has been established; the fuzz harness checks
that stronger diagnostic property explicitly.

## Fix

[`../../asn1scc-patches/reference/161cc246/0011-external-length-error-code.patch`](../../asn1scc-patches/reference/161cc246/0011-external-length-error-code.patch)
moves the existing error assignment after the guarded decode in the C and
Rust variable-size and fixed-size external OCTET STRING templates. One
assignment then covers bounds rejection and contents failure. It also
covers the variable-size branch whose determinant needs no bounds check.
No decoder predicates, buffer handling, model constraints, or harness
assertions change.

## Reproducers

The consistently named `min.asn1`, `min.acn`, `repro.c`, and `repro.rs`
cover external lengths for SIZE (1..8), SIZE (0..8), and SIZE (2).
Determinants 0, 1, 2, 8, 9, and 255 are checked against input lengths 0..9.
Rejected lengths and truncated contents must fail with a nonzero error;
valid complete inputs must succeed with error zero and preserve count and
contents. The C reproducer also checks consumed length.

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" sh run.sh
ASN1SCC="dotnet /path/to/asn1scc.dll" sh run_rust.sh
```

Both scripts exercise legacy ACN and `--acn-v2`. The Rust script needs Cargo
and dependencies available offline. Upstream wire case
`25-ACNV2-BOUNDARIES/025` contains the C reproducer. The repository compiler
build also invokes the standalone C reproducer.
