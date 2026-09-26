# -icdPdus drops the init function of a referenced non-complex type

asn1scc 4.9.3.0 (161cc2465b568685c09b0a218149fb514ea2a95e), C backend, with
or without `-ACN`.

## Summary

With `-icdPdus`, asn1scc generates only the functions of the listed PDUs
and of the functions they call, following the call graph it records while
building them. For a SEQUENCE child whose type is a type assignment that is
not complex (an OCTET STRING, INTEGER, ... with its own typedef), the
parent's init function calls the child type's init function, but the call
is not recorded. That init function is then not generated, and the code
does not link:

```
Function InitFunctionType will not be generated for Min.Mask
min.c:36:9: warning: implicit declaration of function 'Mask_Initialize'
undefined reference to `Mask_Initialize'
```

Found with esajpip's model, where the Reader Requirements box's masks
(`RreqMask ::= OCTET STRING (SIZE (1 | 2 | 4 | 8))`) and a trailing `Extra`
field are such types, under the reader's PDUs.

## Reproducer

`min.asn1`:

```asn1
Min DEFINITIONS AUTOMATIC TAGS ::= BEGIN
    Mask ::= OCTET STRING (SIZE (1..4))
    Rec ::= SEQUENCE { m Mask }
END
```

`repro.c` calls `Rec_Initialize`. `run.sh` generates `min.asn1` with
`asn1scc -c -icdPdus Rec` and links `repro.c` against it:

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" ./run.sh
```

With 4.9.3.0: the link error above. Expected: `Rec initialized, valid=1`.
Listing `Mask` as a PDU too (`-icdPdus Rec,Mask`) works around it.

## Cause

`BackendAst/DAstInitialize.fs`, the init function of a reference type
(`createReferenceType`): the call from the enclosing type assignment's init
function to the referenced one is recorded (`addFunctionCallToState`) only
when the type is complex (`t.isComplexType`). But the SEQUENCE init
function, `handleChild`, calls the child type's init function for every
child whose type is a type definition outside the runtime library
(`ReferenceToExistingDefinition rf when not rf.definedInRtl`), complex or
not. `DAstConstruction.fs` then prunes by the recorded calls.

## Fix

[`../../asn1scc-patches/0004-icdpdus-reference-init.patch`](../../asn1scc-patches/0004-icdpdus-reference-init.patch)
(after the three deferred-determinant patches in `series`): record the call
for every reference, before the `isComplexType` match. Recording a call
only makes more functions generated, and only with `-icdPdus`, the one user
of the call graph for init functions. Regression check:
`v4Tests/test-cases/icd-pdus/001.asn1` and `001_test.c`, run by the new
`v4Tests/scripts/runIcdPdusTests.sh` (`spec/build-asn1scc.sh` runs it).

## Verification in the initial investigation

`BackendAst.dll` rebuilt from the patched source against the prebuilt
4.9.3.0 assemblies (with the other three patches):

- `run.sh`: the link error before, `Rec initialized, valid=1` after.
- `runIcdPdusTests.sh`: fails before, passes after.
- esajpip's model: without `-icdPdus`, identical output; with the reader's
  PDUs, it links without listing `RreqMask` and `Extra`, and the generated
  code differs from the workaround's only in the standalone ACN functions of
  those two types, which the workaround had made PDUs.

The upstream `regression` tool does not use `-icdPdus`.

## Revalidation against the pinned source (2026-09-26)

The complete unmodified `161cc246` compiler still generates a call to
`Mask_Initialize` without its definition, so `run.sh` fails to link. The
same failure remains with the first three patches. Adding
`0004-icdpdus-reference-init.patch` makes `run.sh` print
`Rec initialized, valid=1`, and the dedicated `runIcdPdusTests.sh`
regression passes. `spec/check-model.sh` also confirms that the committed
reader code in `lib/generated/` matches the fully patched compiler.
