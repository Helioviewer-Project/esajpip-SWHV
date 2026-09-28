#!/bin/sh
# Generates min.asn1/min.acn with asn1scc (ACN v2) and runs repro.c under
# UBSan. Exit status 0: no uninitialized read; nonzero: the bug.
#   ASN1SCC="dotnet /path/to/asn1scc.dll" ./run.sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
asn1scc=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-repro.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
$asn1scc -c -ACN --acn-v2 -o "$out" "$here/min.asn1" "$here/min.acn" 2>/dev/null
grep -n "Msg_a_more_tmp" "$out/min.c"
cc -std=c11 -g -O0 -fsanitize=bool -fno-sanitize-recover=bool -I "$out" \
    "$here/repro.c" "$out"/*.c -o "$out/repro"
"$out/repro"
