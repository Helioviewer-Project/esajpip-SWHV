# ACN v2: a fixed-size deferred determinant uses a nonexistent member

With `--acn-v2`, a deferred size determinant for a fixed-size `OCTET
STRING` is generated from `pVal->data.nCount` or `pVal->head.nCount`.
Fixed-size C fields have no `nCount`, so the generated C does not compile.

`min.asn1` and `min.acn` give both a referenced child and a sibling a
two-byte `OCTET STRING`. Compiling the generated `min.c` is the reproducer;
no separate C source is needed. Run it with:

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" sh run.sh
```

The failure is isolated with patches `0001` through `0004` applied and
`0005` absent. [`0005-deferred-fixed-size-determinant.patch`](../../asn1scc-patches/0005-deferred-fixed-size-determinant.patch)
uses the declared ACN size when its minimum and maximum are equal; values
with variable size still use their runtime `nCount`. This matches the
existing nondeferred size rule in `BackendAst/Acn/AcnDependencies.fs`.

The upstream `25-ACNV2-BOUNDARIES/019` test compiles the generated C and
checks exact wire bytes and a round trip for both determinant consumers.
