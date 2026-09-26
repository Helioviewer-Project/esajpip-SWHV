# ACN v2 C decoder reads an uninitialized temporary when a deferred determinant fails to decode

asn1scc 4.9.3.0 (161cc2465b568685c09b0a218149fb514ea2a95e), C backend,
`--acn-v2`.

## Summary

When an ACN inserted field in a referenced type is a determinant of a
sibling in the enclosing type (a deferred determinant), the generated C
decoder decodes the field into a local temporary and then copies it into
the `AcnInsertedFieldRef`. The copy runs even when decoding the field
failed. At end of stream `BitStream_ReadBit` returns FALSE without writing
its output, so the copy reads an uninitialized `flag`. That is undefined
behavior; UBSan reports it as

    runtime error: load of value 79, which is not a valid value for type '_Bool'

The decode still returns FALSE, so the copied value is never used, but any
decoder of such a type can be made to execute the read with truncated input.

## Reproducer

`min.asn1`:

```asn1
Min DEFINITIONS AUTOMATIC TAGS ::= BEGIN
    First ::= SEQUENCE { bits INTEGER (0..127) }
    Last ::= SEQUENCE { bits INTEGER (0..127) }
    Msg ::= SEQUENCE { a First, b Last OPTIONAL }
END
```

`min.acn`:

```
Min DEFINITIONS ::= BEGIN
    First [] { more BOOLEAN [true-value '1'B], bits [encoding pos-int, size 7] }
    Last [] { bits [encoding pos-int, size 8] }
    Msg [] { a [], b [present-when a.more] }
END
```

(The same construct is `v4Tests/test-cases/acn/25-ACNV2-BOUNDARIES/001`.)

`repro.c` decodes `Msg` from an empty buffer. `run.sh` generates the code,
builds it with `-fsanitize=bool` and runs it:

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" ./run.sh
```

Generated `Msg_a_ACN_Decode` (`asn1scc -c -ACN --acn-v2`):

```c
flag Msg_a_more_tmp;
(void)Msg_a_more;

/*Decode Msg_a_more */
ret = BitStream_ReadBit(pBitStrm, (&(Msg_a_more_tmp)));
*pErrCode = ret ? 0 : ERR_ACN_DECODE_MSG_A_MORE;
Msg_a_more->value = (asn1SccUint)Msg_a_more_tmp;
```

Output with 4.9.3.0 (gcc 11.4, `-O0 -fsanitize=bool`):

```
min.c:206:22: runtime error: load of value 79, which is not a valid value for type '_Bool'
```

Expected: `0 byte(s): decode ok=0 err=24` and no sanitizer report.

## Cause

`BackendAst/DAstACNDeferred.fs`, decode branch "Decode to a clean temp
variable, then copy to det.value" (the `temp_copy` redirect, used for
`AcnBoolean`, `AcnReferenceToEnumerated` and slim `AcnInteger` types):

- the temporary is declared without an initializer
  (`BooleanLocalVariable (tmpName, None)`, or `initExp = None`);
- the copy is appended unconditionally:
  `funcBody = r.funcBody + "\n" + assignStmt`.

The C copy templates, `acn_deferred_det_copy_tmp_decode`,
`acn_deferred_det_copy_bool_tmp_decode` and
`acn_deferred_det_copy_enum_tmp_decode` in `StgC/acn_c.stg`, emit a plain
assignment.

Other backends, from the same input: the Ada decoder calls
`UPER_Dec_boolean`, whose `BitStream_ReadBit` assigns `Bit_Value` on both
paths, so the boolean temporary is written even on failure (enumerated and
slim integer temporaries were not checked). The Rust decoder declares its
variable initialized (`let mut Msg_a_more: bool = false;`).

## Fix

[`../../asn1scc-patches/deferred-determinant-uninit.patch`](../../asn1scc-patches/deferred-determinant-uninit.patch)
(applies to 161cc246; `spec/build-asn1scc.sh` applies it until upstream
has the fix):

- `StgC/acn_c.stg`: the three C copy templates emit the copy only when the
  decode succeeded, `if (ret) { ... }`, as the surrounding C templates do.
  On failure `det.value` keeps its `{0}` initializer, and the caller does
  not use it because it checks `ret` first.
- `v4Tests/test-cases/acn/25-ACNV2-BOUNDARIES/001_truncated_test.c` and
  `v4Tests/scripts/runWireTests.sh`: a regression check that decodes test
  case 001 from truncated input (0 bytes, and `0x80` alone) with
  `-fsanitize=bool`.

An alternative fix in `DAstACNDeferred.fs` would give the temporary an
initializer, but that needs a per-type initial value (a valid enumerant
for enumerated types), while the guard needs none.

## Verification in the initial investigation

With the pinned 4.9.3.0 build, using the patched `acn_c.stg` (asn1scc loads
`.stg` files from the current directory before its own):

- `run.sh`: the unpatched templates give the runtime error above, the
  patched ones print `0 byte(s): decode ok=0 err=24` with no report.
- `runWireTests.sh` with the patch: the new check fails with the unpatched
  templates (`001.c:231:28: runtime error: load of value 79, ...`) and
  passes with the patched ones; wire 004, 010, 011 and the warning checks
  still pass.
- `25-ACNV2-BOUNDARIES` with `-atc`, built with
  `-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter`: the same
  outcomes with and without the patch. The generated code changes only in
  001, 006 and 007 (one guarded copy each), and their automatic test cases
  pass.
- A JPEG 2000 model (PLT packet lengths, nine chained `more` determinants):
  the generated code changes only in those nine copies. A libFuzzer corpus
  that reproduces the report at once with the unpatched code runs clean
  with the patched code.

At that point the compiler had not been rebuilt as a whole, and
`regression` with `-acnv2` had not been run.

## Revalidation against the pinned source (2026-09-26)

The unmodified `161cc246` compiler still reports the Boolean load under
`-fsanitize=bool`. A complete Docker build with all patches in `series`
prints `0 byte(s): decode ok=0 err=24` without a sanitizer report. Its
`25-ACNV2-BOUNDARIES` regression, `runWireTests.sh`, and the full model gate
in `spec/check-model.sh` pass. The model gate also confirms that
`lib/generated/` matches this compiler.

## Also noticed in Rust (separate)

The pinned compiler still generates a Rust `Msg_a_ACN_Decode` that declares
`let mut Msg_a_more: bool = false;`, which shadows its
`Msg_a_more: &mut acn::AcnInsertedFieldRef` parameter, and never writes
the parameter. `Msg_ACN_Decode` then reads `Msg_a_more.value` from its
default, so it would not decode `b`. This was verified in generated source;
the Rust output was not compiled or run.

The same `min.asn1` and `min.acn` reproduce the generated source from this
directory, with `ASN1SCC` set as in the C reproducer:

```sh
out=$(mktemp -d)
$ASN1SCC -Rust -ACN --acn-v2 -o "$out" min.asn1 min.acn
grep -n 'Msg_a_more' "$out/min.rs"
```
