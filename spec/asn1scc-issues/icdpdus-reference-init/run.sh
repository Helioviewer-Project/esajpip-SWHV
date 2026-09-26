#!/bin/sh
# Generates min.asn1 with asn1scc -icdPdus Rec and links repro.c against it.
# Exit status 0: it links and runs; nonzero: the bug.
#   ASN1SCC="dotnet /path/to/asn1scc.dll" ./run.sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
asn1scc=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-repro.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
$asn1scc -c -icdPdus Rec -o "$out" "$here/min.asn1" | grep "will not be generated" || true
cc -std=c11 -I "$out" "$here/repro.c" "$out"/*.c -o "$out/repro"
"$out/repro"
