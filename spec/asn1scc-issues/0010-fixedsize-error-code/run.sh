#!/bin/sh
# Compile and run the C reproducer with both ACN generators and uPER.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-fixedsize-error.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
for mode in legacy v2; do
    generated=$out/$mode
    mkdir "$generated"
    set --
    [ "$mode" != v2 ] || set -- --acn-v2
    $compiler -c -uPER -ACN "$@" -fp AUTO -o "$generated" "$here/min.asn1" "$here/min.acn"
    cc -std=c11 -Wall -Wextra -Werror -I "$generated" \
        "$here/repro.c" "$generated"/*.c -o "$generated/repro"
    "$generated/repro"
done
