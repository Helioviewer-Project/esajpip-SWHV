# A CONTAINING region longer than its stream is read past the buffer

With `--acn-v2`, an `OCTET STRING (CONTAINING X)` sized by an ACN length
determinant (`body [size len]`) is decoded in place: the generated decoder
narrows the stream to the region and decodes X there. The C template
(`StgC/acn_c.stg`, around the `acn_expected_end` lines) does

```c
long acn_saved_count = pBitStrm->count;
long acn_expected_end = pBitStrm->currentByte + (long)<len>->value;
pBitStrm->count = acn_expected_end;
```

without checking that `acn_expected_end <= acn_saved_count`. A length
larger than what is left of the enclosing stream therefore raises
`count` past the end of the buffer, and every read inside the region
(`BitStream_ReadBit`, `BitStream_DecodeOctetString_no_length`, ...)
bounds-checks against that raised `count`: it reads past the buffer.

`min.asn1`/`min.acn` is a one-byte length and a region holding an
`OCTET STRING` that fills it. `repro.c` decodes a 3-byte heap buffer whose
length byte says 10. Run it with:

```sh
ASN1SCC="dotnet /path/to/asn1scc.dll" sh run.sh
```

With asn1scc 4.9.3.0 and patches 0001 to 0007, AddressSanitizer reports a
heap-buffer-overflow: a READ of size 10 in `memcpy` from
`BitStream_DecodeOctetString_no_length`, one byte into the 3-byte buffer.
After the fix the decoder fails (`decode failed`) and the exit status is 0.

Fix: compare the region length with the bytes or bits remaining in the
enclosing stream before narrowing `count`. This avoids overflow in the
end-position calculation and rejects the malformed length with the field's
error code. The external determinant, wrapper, and embedded length templates
for both `OCTET STRING` and `BIT STRING` need the check.

Where it showed in this repository: a differential fuzz of the corpus
(random byte mutations, each labelled by the harness and checked by the
reader) crashed the harness under AddressSanitizer in
`MainSegment_tilePart_rest_Containing_ACN_Decode` and the Iplt decoder, on
mutants whose Psot or Lxxx grew past the file. The reader in `lib/` is not
exposed: it decodes `CodSegment` and `QcdSegment` only with the region
length it has already checked against the buffer. The corpus is not
affected: its length mutants stay within the file.

The same unchecked stream-limit change also occurs in the deferred
`BIT STRING (CONTAINING ...)` C and Rust templates. Patch 0008 covers both
string kinds in C and Rust. Ada keeps the buffer size fixed rather than
narrowing its limit, so this particular out-of-buffer read does not follow
from its templates.

The Rust checks, `has_n_bytes` and `has_n_bits`, are methods of
`BitStream` in `asn1rust/src/lib.rs`, the stream the generated decoders
are given. The Rust ACN v2 code of test case 022 still does not compile,
for reasons outside this patch: its encoders call a deferred
patch function whose name the Rust backend leaves empty
(`ret = (acn_n_count, pBitStrm, OuterOct_body_len, pErrCode);`), and its
decoders do not pass the decoded length to the region, whose length stays 0.

The C reproducer and wire case 022 pass with the rebuilt compiler.
The corrected Rust runtime compiles, but the complete Rust regression
for this bug remains blocked by the case 022 generation errors above.
