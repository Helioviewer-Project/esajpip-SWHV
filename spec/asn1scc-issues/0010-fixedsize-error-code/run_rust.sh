#!/bin/sh
# Compile and run the Rust reproducer with both ACN generators and uPER.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-fixedsize-error-rust.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
for mode in legacy v2; do
    generated=$out/$mode
    mkdir "$generated"
    set --
    [ "$mode" != v2 ] || set -- --acn-v2
    $compiler -Rust -uPER -ACN "$@" -fp AUTO -o "$generated" "$here/min.asn1" "$here/min.acn"
    cp "$here/repro.rs" "$generated/mainprogram.rs"
    (cd "$generated" && cargo run -q --offline)
done
