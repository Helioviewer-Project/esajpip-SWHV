#!/bin/sh
# Exit status 0 means every case encodes right.
# ASN1SCC="dotnet /path/to/asn1scc.dll" ./run.sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-absent-producer.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
$compiler -c -ACN --acn-v2 -fp AUTO -o "$out" "$here/min.asn1" "$here/min.acn"
cc -std=c11 -I "$out" "$here/repro.c" "$out"/*.c -o "$out/repro"
"$out/repro"
