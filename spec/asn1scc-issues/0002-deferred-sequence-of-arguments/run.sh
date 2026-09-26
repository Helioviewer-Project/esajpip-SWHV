#!/bin/sh
# Generates min.asn1/min.acn with ACN v2 and runs its reproducer under
# ASan and UBSan. Exit status 0 means this case works.
#   ASN1SCC="dotnet /path/to/asn1scc.dll" ./run.sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
asn1scc=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-repro.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
$asn1scc -c -ACN --acn-v2 -o "$out" "$here/min.asn1" "$here/min.acn"
cc -std=c11 -g -O0 -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I "$out" "$here/repro.c" "$out"/*.c -o "$out/repro"
"$out/repro"
