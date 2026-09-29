# CONTAINING loses a referenced subtype's constraints

## Upstream status

Fixed upstream in ASN1SCC 4.9.7.0, pinned at
`1afedf0c205c1fb783245e1f2d1e06cacc06d042`, as part of
[issue #417](https://github.com/esa/asn1scc/issues/417).
The patch is retired under
[`../../asn1scc-patches/reference/161cc246/`](../../asn1scc-patches/reference/161cc246/).
The report below describes the original failure; the reproducer is retained
and passes with the unmodified pinned upstream compiler.

`X-P` restricts `X.field` to zero. `W.body` contains `X-P` and has an
ACN-inserted byte length. Generate and run the example with:

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" sh run.sh
```

Before the fix, the generated `W_IsConstraintValid` calls
`X_IsConstraintValid` rather than `X_P_IsConstraintValid`. Its decoder
also accepts `{1, 1}`. The reproducer prints
`invalid validity=1 decode=1; valid decode=1` and exits with status 1.
The subtype's standalone validator and decoder do reject `field=1`.

The expected result after the fix is
`invalid validity=0 decode=0; valid decode=1` with exit status 0.
