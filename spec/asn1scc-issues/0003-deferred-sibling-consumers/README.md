# ACN v2: a sibling also consumes a deferred determinant

With `--acn-v2`, an ACN determinant may be deferred because a referenced
child produces it. When a sibling field also consumes that determinant, the
generated decoder reads the ordinary variable `Msg_len`, although the
deferred path has replaced that variable with `len.value`. The C code does
not compile. The encoder also fails to compare the sibling's size with the
value produced by the referenced child.

`min.asn1` and `min.acn` use one length for `head` and
`payload.data`. `repro.c` tests a matching two-byte pair and then a
three-byte `head` beside two bytes of data. Run it with:

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" sh run.sh
```

Before patch `0003`, the generated C fails to compile because `Msg_len` is
undeclared. After the patch, the two-byte case encodes as
`02 AA AA BB BB` and decodes, while the mismatched case is rejected.

[`0003-deferred-sibling-consumers.patch`](../../asn1scc-patches/0003-deferred-sibling-consumers.patch)
keeps the ordinary decoded variable for sibling consumers and copies its
value into `len.value`. The encoder patches the determinant from each
sibling consumer, rejecting disagreement with the referenced child's value.
The upstream C regressions are `25-ACNV2-BOUNDARIES/017` and `018`, with
wire checks in `v4Tests/scripts/runWireTests.sh`. Case `018` combines this
failure with [patch 0002](../0002-deferred-sequence-of-arguments/) in the
JPEG 2000 Reader Requirements box.

With the complete patched compiler, the successful encode returns true but
prints `err=203`: a consumer's `PatchDet` sets the error code on success.
The wire bytes and return flag are correct; this is a separate status-output
inconsistency.
