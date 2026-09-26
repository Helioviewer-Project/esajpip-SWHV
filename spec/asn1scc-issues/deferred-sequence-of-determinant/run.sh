#!/bin/sh
# Generates min.asn1/min.acn and sibling.asn1/sibling.acn with asn1scc
# (ACN v2) and runs repro.c and repro-sibling.c under ASan and UBSan.
# Exit status 0: both work; nonzero: a bug.
#   ASN1SCC="dotnet /path/to/asn1scc.dll" ./run.sh
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
asn1scc=${ASN1SCC:-asn1scc}
out=$(mktemp -d "${TMPDIR:-/tmp}/asn1scc-repro.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
status=0
for case in min:repro sibling:repro-sibling; do
    name=${case%%:*} test=${case#*:}
    mkdir "$out/$name"
    echo "== $name"
    $asn1scc -c -ACN --acn-v2 -o "$out/$name" "$here/$name.asn1" "$here/$name.acn" 2>/dev/null
    if cc -std=c11 -g -O0 -fsanitize=address,undefined -fno-sanitize-recover=all \
           -I "$out/$name" "$here/$test.c" "$out/$name"/*.c -o "$out/$name/repro" \
           2>"$out/$name/cc.log"; then
        "$out/$name/repro" > "$out/$name/run.log" 2>&1 || status=1
        grep -v "^    #\|^$\|^==.*Hint\|^AddressSanitizer can" "$out/$name/run.log" | head -8
    else
        grep -m1 "error:" "$out/$name/cc.log"
        status=1
    fi
done
exit $status
