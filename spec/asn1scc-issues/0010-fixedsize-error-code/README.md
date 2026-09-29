# Fixed-size OCTET STRING decode fails without an error code

## Upstream status

Fixed upstream in ASN1SCC 4.9.7.0, pinned at
`1afedf0c205c1fb783245e1f2d1e06cacc06d042`, as part of
[issue #417](https://github.com/esa/asn1scc/issues/417).
The patch is retired under
[`../../asn1scc-patches/reference/161cc246/`](../../asn1scc-patches/reference/161cc246/).
The report below describes the original failure; the reproducer is retained
and passes with the unmodified pinned upstream compiler.

asn1scc 4.9.3.0, pinned revision
`161cc2465b568685c09b0a218149fb514ea2a95e`, with patches 0001 through 0009.

## Cause

The C `octet_FixedSize_decode` template in `StgC/uper_c.stg` calls
`BitStream_DecodeOctetString_no_length` without assigning an error code.
The enclosing decoder initializes the error to zero. When fewer than the
required bytes remain, the runtime returns false and the decoder returns
false with error zero. Its final constraint check is short-circuited.
The Rust template has the same omission. The Ada template initializes a
successful result with error zero and likewise never assigns an error when
the runtime changes the success flag to false.

Both uPER and fixed-size ACN use this template. Patch 0009 addresses the
variable-size template and does not cover this path. Rejection itself works;
the defect is the missing failure diagnostic.

## Reproducer

`min.asn1` and `min.acn` define a standalone 16-byte OCTET STRING and an
inline 16-byte field in a SEQUENCE. `repro.c` and `repro.rs` check every
input length from 0 through 16, using uPER and ACN. Short inputs must fail
with a nonzero error. The complete input must succeed with error zero and
preserve all bytes. The inline field exercises a field-specific error code.

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" sh run.sh
ASN1SCC="dotnet /path/to/asn1scc.dll" sh run_rust.sh
```

Both scripts cover legacy ACN and `--acn-v2`. The Rust script needs Cargo
and the generated runtime dependencies available offline.

## Fix

[`../../asn1scc-patches/reference/161cc246/0010-fixedsize-error-code.patch`](../../asn1scc-patches/reference/161cc246/0010-fixedsize-error-code.patch)
passes the allocated error code through both generator call sites to the
fixed-size template. C, Rust, and Ada assign it on failure. All backends'
encode/decode signatures accept the new argument to keep the shared macro
interface consistent; Python and Scala output and encoder output are unchanged.
The build regenerates the macro interface and adapters from those signatures.

Upstream case `25-ACNV2-BOUNDARIES/024` and its wire test cover the C ACN
path. `spec/build-asn1scc.sh` also invokes the standalone C reproducer.

## Repository impact

The reported fuzz failure was `UuidId: decoded=0 error=0`. Source inspection
confirms it in `jpeg2000/generated/jp2-boxes.c`; `VendorId` uses the same template.
Both checked-in decoders receive the template's error assignment. The model
and the fuzz assertion are unchanged.
