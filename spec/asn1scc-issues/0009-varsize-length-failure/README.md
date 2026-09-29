# A variable-size string decodes after its length fails to

asn1scc 4.9.3.0 (161cc2465b568685c09b0a218149fb514ea2a95e), C and Rust
backends, uPER and ACN (a type without ACN encoding attributes is decoded by
the uPER templates).

## Summary

The decoder of a variable-size OCTET STRING, `octet_VarSize_decode` in
`StgC/uper_c.stg`, is

```c
ret = BitStream_DecodeConstraintWholeNumber(pBitStrm, &nCount, <nSizeMin>, <nSizeMax>);
*pErrCode = ret ? 0 : <sErrCode>;
<p><sAcc>nCount = (long)nCount;
ret = BitStream_DecodeOctetString_no_length(pBitStrm, <p><sAcc>arr, <p><sAcc>nCount);
```

The contents are decoded whatever the length decode returned, and `ret` is
overwritten. When the length does not decode (it is above the size bound,
or the input ends), the runtime has set `nCount` to 0, so zero bytes are
read and the decode succeeds. Where the size constraint allows 0, the
decoder then accepts the input as an empty string, and the constraint
check it ends with resets the error code to 0. With no input the stream's
buffer is NULL, and the zero-byte copy
`memcpy(arr, &pBitStrm->buf[pBitStrm->currentByte], 0)` is undefined
behavior. `StgRust/uper_rust.stg` has the same sequence.

Two related gaps leave a failed decode with error code 0: the OCTET STRING
decoder sets no error code when the contents fail to decode, and
`bitString_VarSize_decode` (C and Rust), which does guard its contents by
`ret`, sets none when the length fails. The Ada template
(`StgAda/uper_a.stg`) guards the contents and is not affected.

## Reproducer

`min.asn1` and `min.acn` are an `OCTET STRING (SIZE (0..8))` and a
`BIT STRING (SIZE (0..8))`, both with a 4-bit length. `repro.c` decodes each
with its uPER and ACN decoders from the byte `0xF0` (length 15) and from no
input; `run.sh` builds it with UBSan:

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" sh run.sh
```

With 4.9.3.0 and patches 0001 to 0008, UBSan stops the run:

```
asn1crt_encoding.c:922:13: runtime error: null pointer passed as argument 2, which is declared to never be null
```

Built without UBSan, `repro.c` prints

```
Mask uPER, 1 bytes: decoded=1 error=0
Mask ACN, 1 bytes: decoded=1 error=0
Bits uPER, 1 bytes: decoded=0 error=0
Bits ACN, 1 bytes: decoded=0 error=0
Mask uPER, 0 bytes: decoded=1 error=0
Mask ACN, 0 bytes: decoded=1 error=0
Bits uPER, 0 bytes: decoded=0 error=0
Bits ACN, 0 bytes: decoded=0 error=0
```

and exits 1. After the fix every decode fails with the type's error code
(`Mask uPER, 1 bytes: decoded=0 error=3`, ...) and the exit status is 0.
`repro.rs` and `run_rust.sh` are the same check against the Rust backend,
with the same results before and after; the Rust runtime compiles only with
the `BitStream` stream checks of patch 0008.

## Fix

[`../../asn1scc-patches/0009-varsize-length-failure.patch`](../../asn1scc-patches/0009-varsize-length-failure.patch),
in C and Rust: the OCTET STRING contents are decoded only when the length
decoded, and set the error code when they fail; the BIT STRING decoder sets
the error code when its length fails. Test case
`25-ACNV2-BOUNDARIES/023` and its wire test in
`v4Tests/scripts/runWireTests.sh`, built with UBSan, check a round trip of
each type with its exact bytes, and that a length of 15, no input and short
contents each fail with the type's error code.

## Where it showed in this repository

`fuzz_asn1`, replayed under UBSan (`fuzz_replay_asn1`), in
`RreqMask_ACN_Decode`: the standalone decoder of the Reader Requirements
mask, `RreqMask ::= OCTET STRING (SIZE (1 | 2 | 4 | 8))`. The library does
not call it: it decodes the masks through `RreqHeader_fuam_ACN_Decode` and
`RreqHeader_dcm_ACN_Decode`, which take the length from the header and
guard the contents. `jpeg2000/generated` changes only in `RreqMask_ACN_Decode`.

## Verification

With the templates changed as in the patch (the compiler reads its `.stg`
files at run time): `run.sh` and `run_rust.sh` pass; `runWireTests.sh`
fails at 023 before and passes after; `runIcdPdusTests.sh` passes;
`jpeg2000/generate.sh` changes only `RreqMask_ACN_Decode`, and esajpip's tests
pass under ASan and UBSan with that output.

Independent validation on 2026-09-28 rebuilt the pinned revision with the full
series. The previous 0008 image reproduces the C UBSan failure. With corrected
0008 and without 0009, Rust reproduces the results above; applying 0009 makes
both standalone reproducers pass. The ACN v2 regression suite, wire cases
(including 022 and 023), and `-icdPdus` regressions pass. Regenerating the
library matches the checked-in change to `RreqMask_ACN_Decode` exactly.
`spec/build-asn1scc.sh` now also runs the standalone C reproducers for 0008
and 0009, including the latter's uPER checks.

The full model gate also passes (strict C11, ASan/UBSan, corpus labels,
writer outputs and OpenJPEG), and regenerated corpus files are unchanged.
All 23 repository tests pass under ASan/UBSan, including ASN.1 replay.
The macOS validation build used `-D_DARWIN_C_SOURCE` for the existing
`mkdtemp` declaration issue in the merge replay harness; the two socket
tests were rerun outside the filesystem/network sandbox.
