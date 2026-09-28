#!/bin/sh
# Exit status 0 means every decode of a length that does not decode fails,
# with an error code, and UBSan reports nothing.
#   ASN1SCC="dotnet /path/to/asn1scc.dll" sh run.sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-varsize-length.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
$compiler -c -uPER -ACN -fp AUTO -o "$out" "$here/min.asn1" "$here/min.acn"
cc -std=c11 -g -fsanitize=undefined -fno-sanitize-recover=undefined -I "$out" \
    "$here/repro.c" "$out"/*.c -o "$out/repro"
"$out/repro"
