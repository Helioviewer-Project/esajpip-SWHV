# CONTAINING loses a referenced subtype's constraints

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
