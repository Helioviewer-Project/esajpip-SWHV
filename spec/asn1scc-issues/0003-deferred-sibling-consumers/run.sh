#!/bin/sh
# Exit status 0 means the sibling case compiles and behaves correctly.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-sibling.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
$compiler -c -ACN --acn-v2 -o "$out" "$here/min.asn1" "$here/min.acn"
cc -std=c11 -g -O0 -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I "$out" "$here/repro.c" "$out"/*.c -o "$out/repro"
"$out/repro"
