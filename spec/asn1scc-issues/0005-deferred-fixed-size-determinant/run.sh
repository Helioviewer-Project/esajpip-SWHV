#!/bin/sh
# Exit status 0 means the generated fixed-size C compiles.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-fixed-size.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
$compiler -c -ACN --acn-v2 -o "$out" "$here/min.asn1" "$here/min.acn"
cc -std=c11 -I "$out" -c "$out/min.c" -o "$out/min.o"
