#!/bin/sh
# repro.rs against the Rust backend. Exit status 0 as for run.sh.
#   ASN1SCC="dotnet /path/to/asn1scc.dll" sh run_rust.sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-varsize-length-rust.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
$compiler -Rust -uPER -ACN -fp AUTO -o "$out" "$here/min.asn1" "$here/min.acn"
cp "$here/repro.rs" "$out/mainprogram.rs"
(cd "$out" && cargo run -q --offline)
