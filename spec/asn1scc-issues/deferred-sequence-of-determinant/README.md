# ACN v2: a determinant passed to SEQUENCE OF elements, or also used by a sibling, is not handled as deferred

asn1scc 4.9.3.0 (161cc2465b568685c09b0a218149fb514ea2a95e), C backend,
`--acn-v2`.

## Summary

With `--acn-v2`, an ACN inserted field of a SEQUENCE that is passed as an
argument to a child of a reference type is a deferred determinant: the
SEQUENCE's encoder reserves its bits (`Acn_InitDet_*`), the child's encoder
patches them (`Acn_PatchDet_*`), and the decoder decodes it into the
`AcnInsertedFieldRef` it passes down. Two cases miss this:

1. **The argument goes to the elements of a SEQUENCE OF child**
   (`items [size count] { <len> [] }`). The SEQUENCE does not treat `len`
   as deferred. Its encoder computes `len` the ordinary way, from the
   element's size, but outside the element loop, so it reads
   `pVal->items.arr[i1]` with `i1` uninitialized:

   ```c
   int i1;
   ...
   Msg_len_is_initialized = TRUE;
   Msg_len = pVal->items.arr[i1].data.nCount;
   ```

   It then writes `len` directly, while each element encoder still calls
   `Acn_PatchDet_U8` on an `AcnInsertedFieldRef` that no `InitDet` set up
   (position 0 of the stream). With optimization gcc reports
   `'i1' may be used uninitialized`; at run time the read can crash or
   produce a wrong determinant. A list that may be empty has no element to
   take `len` from at all.

2. **The determinant is also consumed by a sibling** (`head [size len]`
   beside `payload <len> []`). The SEQUENCE treats `len` as deferred and
   decodes it into `len.value`, but the sibling's decoder still reads the
   ordinary variable `Msg_len`, which is no longer declared, so the
   generated C does not compile:

   ```c
   ret = Acn_Dec_Int_PositiveInteger_ConstSize_8(pBitStrm, (&(len.value)));
   ...
   ret = (1<=Msg_len && Msg_len<=4);
   ```

   The encoder writes `head` without patching or checking `len`: `len` is
   set from `payload` alone, so a `head` of another size gives a wrong
   encoding without an error; and where the consumers inside children are
   absent (an empty list, in the Reader Requirements box below), `len`
   stays at the fallback, 0, whatever the size of `head`.

Both occur in the Reader Requirements box of JPEG 2000 (ITU-T T.801
M.11.1): ML, the mask length, sizes two masks and the mask of every entry
of two lists (`../../asn1scc-patches/`, test case 018).

## Reproducer

`min.asn1` / `min.acn` (case 1):

```asn1
Min DEFINITIONS AUTOMATIC TAGS ::= BEGIN
    Item ::= SEQUENCE { data OCTET STRING (SIZE (1..4)) }
    Msg ::= SEQUENCE { items SEQUENCE (SIZE (0..4)) OF Item }
END
```

```
Min DEFINITIONS ::= BEGIN
    Item <INTEGER:len> [] { data [size len] }
    Msg [] {
        len INTEGER [encoding pos-int, size 8],
        count INTEGER [encoding pos-int, size 8],
        items [size count] { <len> [] }
    }
END
```

`sibling.asn1` / `sibling.acn` (case 2):

```asn1
Sibling DEFINITIONS AUTOMATIC TAGS ::= BEGIN
    Item ::= SEQUENCE { data OCTET STRING (SIZE (1..4)) }
    Msg ::= SEQUENCE { head OCTET STRING (SIZE (1..4)), payload Item }
END
```

```
Sibling DEFINITIONS ::= BEGIN
    Item <INTEGER:len> [] { data [size len] }
    Msg [] {
        len INTEGER [encoding pos-int, size 8],
        head [size len],
        payload <len> []
    }
END
```

`repro.c` encodes and decodes a `Msg` of `min` with two 2-byte items;
`repro-sibling.c` encodes and decodes a `Msg` of `sibling` with a 2-byte
`head` and `data`, prints the encoder's error code, then expects a 3-byte
`head` to be rejected. `run.sh`
generates both, builds them with ASan and UBSan and runs them:

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" ./run.sh
```

Output with 4.9.3.0 (gcc 11.4, `-O0`):

```
== min
==36==ERROR: AddressSanitizer: SEGV on unknown address 0x20104d0dc611 ...
SUMMARY: AddressSanitizer: SEGV .../min/min.c:135 in Msg_ACN_Encode
== sibling
.../sibling/sibling.c:180:23: error: 'Msg_len' undeclared (first use in this function)
```

With the two determinant fixes, including the error output discussed below:

```
== min
encode ok=1: 02 02 A0 A0 A1 A1
decode ok=1
== sibling
encode ok=1 err=203: 02 AA AA BB BB
decode ok=1
head 3, data 2: encode rejected
```

## Cause

`BackendAst/DAstACNDeferred.fs`:

1. `collectDeferredDetNames` and `collectDeferredDetNamesFromAst` collect
   the arguments of the SEQUENCE's children that are reference types
   (`ReferenceType rt -> rt.acnArguments`) and ignore every other kind.
   A SEQUENCE OF child passes the arguments of its element type, so its
   determinants are not collected, and `createDeferredSequenceFunction`
   keeps the ordinary update statement, whose access path ends in the
   element (`arr[i1]`). The SEQUENCE OF code itself already passes the
   `AcnInsertedFieldRef` to each element, which patches it.
2. For a local deferred determinant, `createDeferredSequenceFunction`
   replaces the child's encoding by `InitDet` and its decoding by a decode
   into `det.value` (or into `<c_name>_tmp`, then copied), and drops its
   update statement. The consumers that caused the deferral patch it; its
   other consumers, the siblings in the same SEQUENCE, are not considered:
   their decoders name the ordinary variable, `ac.c_name`, and no code
   patches the value from them. (`producerPatchEpilogue` does this for the
   reverse case, a determinant produced by a referenced child and consumed
   by a sibling.)

## Fix

Two patches, applied after `0001-deferred-determinant-uninit.patch` in
`../../asn1scc-patches/series` (`spec/build-asn1scc.sh` applies them until
upstream has the fix):

- [`0002-deferred-sequence-of-arguments.patch`](../../asn1scc-patches/0002-deferred-sequence-of-arguments.patch):
  both collectors look through SEQUENCE OF children, nested ones included,
  to the element's reference type. `len` is then deferred: `InitDet`
  before the list, `PatchDet` in each element (a mismatch fails with
  `ERR_ACN_DET_CONSISTENCY_MISMATCH`), and the existing fallback when no
  element patched it (0 for an empty list). Test case
  `25-ACNV2-BOUNDARIES/016` with `016_wire_test.c` in
  `scripts/runWireTests.sh`.
- [`0003-deferred-sibling-consumers.patch`](../../asn1scc-patches/0003-deferred-sibling-consumers.patch):
  for a local deferred determinant that siblings also consume,
  - decoding: decode into the ordinary variable, `ac.c_name`, and copy it
    to `det.value` (the existing `temp_copy` path, with that name instead
    of `<c_name>_tmp`), so the siblings' decoders find it;
  - encoding: after the children, before the fallback, `PatchDet` from each
    sibling consumer (the value `computePatchDetValueExpr` gives, as in
    `producerPatchEpilogue`), which sets the value or fails with
    `ERR_ACN_DET_CONSISTENCY_MISMATCH` if it disagrees.

  Test cases `017` (case 2) and `018` (both, the Reader Requirements
  shape), with their wire tests.

The three test cases are `NO_AUTOMATIC_TEST_CASES`: the automatic test
values give elements or siblings different sizes, which one determinant
cannot encode. The wire tests check the round trip, the exact bytes, the
empty list and the rejection of disagreeing sizes.

Generated code after the fix, `min`:

```c
/*Encode Msg_len */
Acn_InitDet_U8(pBitStrm, &len);
...
for(i1=0; (i1 < (int)pVal->items.nCount) && ret; i1++)
{
    ret = Msg_items_elm_ACN_Encode((&(pVal->items.arr[i1])), pBitStrm, pErrCode, FALSE, &len);
}
if (ret) {
    if (!len.is_set) {
        Acn_PatchDet_U8((asn1SccUint)0, pBitStrm, &len, pErrCode);
    }
}
```

`sibling`, decoding and encoding:

```c
asn1SccUint Msg_len;
AcnInsertedFieldRef len = {0};

/*Decode Msg_len */
ret = Acn_Dec_Int_PositiveInteger_ConstSize_8(pBitStrm, (&(Msg_len)));
*pErrCode = ret ? 0 : ERR_ACN_DECODE_MSG_LEN;
if (ret) {
    len.value = (asn1SccUint)Msg_len;
}
```

```c
ret = Msg_payload_ACN_Encode((&(pVal->payload)), pBitStrm, pErrCode, FALSE, &len);
if (ret) {
    ret = Acn_PatchDet_U8((asn1SccUint)pVal->head.nCount, pBitStrm, &len, pErrCode);
    ...
```

## Verification in the initial investigation

The pinned 4.9.3.0 build with `BackendAst.dll` rebuilt from the patched
source (`fsc` against the other prebuilt assemblies; NuGet was not
reachable, so the compiler was not rebuilt as a whole):

- `run.sh`: the output above, before and after.
- `scripts/runWireTests.sh`: passes with the patches (wire 004, 010, 011,
  016, 017, 018, truncated 001, the warning checks). Without them the
  generated code of 016 does not compile under its `-Werror`
  (`'i1' may be used uninitialized`); with the first patch alone, 017 and
  018 do not (`'Msg_len'` / `'Requirements_ml' undeclared`).
- The regression's command line (`-c -x ast.xml -uPER -ACN --acn-v2
  -typePrefix ASN1SCC_ -renamePolicy 3 -fp AUTO -equal -atc -fpWordSize 8
  -wordSize 8`) on the ACN cases of that `v4Tests` snapshot (each `.acn`
  file and each `--TCLS` line): byte-identical output with and without the
  patches. 016 to 018 then build with the generated Makefile (`-Wall
  -Wextra -Werror`); without the patches they do not.
- A JPEG 2000 model (esajpip's `spec/`): identical output.

At that point `regression` itself and the other backends had not been run
(the new cases are `C_ONLY`).

## Revalidation against the pinned source (2026-09-26)

Before the fixed-size patch was added, the complete pinned compiler was built
with no patches, the first two patches, the first three patches, and all four
patches. With GCC 13.3.0,
the unpatched `min` encoder emits `02 02 A0 A0 A1 A1`, but its decoder
returns false; the unpatched `sibling` generated C does not compile because
`Msg_len` is undeclared. The first two patches make `min` round-trip, but
`sibling` still does not compile. Adding `0003-deferred-sibling-consumers.patch`
makes both reproducers pass and rejects the mismatched sizes. The complete
patched build passes `25-ACNV2-BOUNDARIES`, `runWireTests.sh`, and
`spec/check-model.sh`. The AddressSanitizer crash above is the outcome
recorded with GCC 11.4.0, not a repeatable outcome on GCC 13.3.0.

## Fixed-size determinant

`fixed.asn1` / `fixed.acn` give the sibling relationship above with
`OCTET STRING (SIZE (2))`. Before
[`0005-deferred-fixed-size-determinant.patch`](../../asn1scc-patches/0005-deferred-fixed-size-determinant.patch),
the generator emitted `pVal->data.nCount` and `pVal->head.nCount`, although
these fixed-size C fields have no `nCount`. The generated C did not compile.

`computePatchDetValueExpr` now emits the declared ACN size when the minimum
and maximum are equal. This follows the existing nondeferred rule in
`BackendAst/Acn/AcnDependencies.fs`; variable-size fields still use their
runtime `nCount`. Test case `25-ACNV2-BOUNDARIES/019` exercises both the
reference child and the direct sibling: its generated C compiles, encodes
the expected bytes, and decodes them back.

The fixed-size reproducer from this directory can also be generated and
compiled with the pinned compiler after applying the patch series:

```sh
out=$(mktemp -d)
$ASN1SCC -c -ACN --acn-v2 -o "$out" fixed.asn1 fixed.acn
cc -std=c11 -I "$out" -c "$out/fixed.c" -o "$out/fixed.o"
```

## Other confirmed behavior (separate from these patches)

- A consumer's `PatchDet` is followed by
  `*pErrCode = ERR_ACN_DET_CONSISTENCY_MISMATCH;` whether or not it
  failed. `repro-sibling.c` now prints `encode ok=1 err=203` on a successful
  encode with the fully patched compiler. The return flag is true and the
  wire bytes are correct. `lib/hv_writer.c` checks that flag, so this has no
  observed effect on esajpip. The compiler's intended meaning for
  `pErrCode` on success is not documented here; this is a status-output
  inconsistency, not a demonstrated encoding failure.
