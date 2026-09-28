# Deferred determinants: an absent producer is patched at bit 0, and the error code is set on success

At upstream commit `3b4e0ffc2d153e01c71964f37a3e73743e320eec`,
`producerPatchEpilogue` still patches absent OPTIONAL producers.
[Commit `d6fc8618`](https://github.com/esa/asn1scc/commit/d6fc8618b08e30e08a811d81538ae494a351e6d4)
already fixes the successful-encode error code in C and Rust. Patch 0007
backports that commit to the pinned compiler and fixes the absent-producer
case.

With `--acn-v2`, a boolean determinant inserted in a referenced type and
consumed by a sibling (`b1 [present-when b0.more]`, where `more` is an ACN
field of `b0`'s type) is deferred: the child's encoder reserves the bit
(`Acn_InitDet_BOOL1`, which records the position in an
`AcnInsertedFieldRef`) and the parent patches it after encoding the
children (`producerPatchEpilogue` in `BackendAst/DAstACNDeferred.fs`). Two
defects in that epilogue:

1. **An absent producer is patched anyway.** The parent patches every
   producer's determinant, whether or not the producing child was encoded.
   For an absent OPTIONAL child the `AcnInsertedFieldRef` local is still
   `{0}`: `Acn_InitDet_BOOL1` never ran, so its position is byte 0, bit 0
   of the bitstream buffer. `Acn_PatchDet_BOOL1` seeks there and writes
   the determinant's value (0, since the consumer is absent too), clearing
   the most significant bit of the buffer's first byte: a byte written
   before this value, or the value's own first `more` bit.
2. **The error code is set on success.** The C templates
   `acn_deferred_det_patch_value_encode` and `acn_deferred_det_patch_ptr_encode`
   in `StgC/acn_c.stg` (and the `_with_size` and `_str` variants) emit

   ```c
   ret = Acn_PatchDet_...(...);
   *pErrCode = ERR_ACN_DET_CONSISTENCY_MISMATCH;
   if (!ret) return FALSE;
   ```

   so every encoder with a deferred determinant returns TRUE with
   `*pErrCode == 203`. `Acn_PatchDet_*` already sets the code when it
   fails.

`min.asn1`/`min.acn` is a four-byte continuation chain (`b0` to `b2` of
type `Link`, whose `more` bit says whether the next byte follows, and a
terminating `b3`). `repro.c` encodes three values after one byte `FF`
already in the stream. Run it with:

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" sh run.sh
```

Output with asn1scc 4.9.3.0 and patches 0001 to 0006:

```
BAD : b0 ret=1 err=203 bytes: 7F 05   expected ret=1 err=0 bytes: FF 05
BAD : b0 b1 ret=1 err=203 bytes: 7F 81 48   expected ret=1 err=0 bytes: FF 81 48
BAD : b0 b1 b2 b3 ret=1 err=203 bytes: FF 81 80 80 05   expected ret=1 err=0 bytes: FF 81 80 80 05
```

exit status 1. The first two values leave `b1`, or `b2`, absent: `FF`
becomes `7F` (defect 1). All three set the error code (defect 2). After the
fix every line starts with `ok` and the exit status is 0.

Encoded at the start of a buffer, defect 1 hits the value itself: a value
of `b0 b1` loses `b0.more` and decodes as `b0` alone.

Where it hit this repository: the JPEG 2000 PLT entry `Iplt`
(`j2k-headers.asn1`) is such a chain of ten bytes. `Iplt_ACN_Encode`
clears bit 0 of the buffer for every entry of 1 to 8 bytes. `hv_writer.c`
encodes each entry on its own, which lost `b0.more` of every 2- to 8-byte
entry, so `append_iplt` encodes after a guard byte. In the whole-file model
the cleared bit is the first bit of the file, 0 in every JP2 and JPX file
(the signature box's LBox), so the corpus never showed it.
