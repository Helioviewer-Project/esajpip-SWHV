#!/bin/sh

set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=${ASN1SCC_IMAGE:-esajpip-asn1scc}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/esajpip-model.XXXXXX")
generated=$temporary/generated
regressions=$temporary/regressions

cleanup() {
    rm -rf "$temporary"
}
trap cleanup EXIT HUP INT TERM

if [ "$#" -gt 1 ]; then
    echo "usage: ASN1SCC_IMAGE=image $0 [corpus-directory]" >&2
    exit 2
fi

mkdir "$generated" "$regressions"

# The decode-only box-type mapping must preserve every modeled TBox value.
known_types=$(sed -n '/^[[:space:]]*other[[:space:]]/!s/.*present-when tbox == \([0-9][0-9]*\),.*/\1/p' \
    "$repo/spec/jp2-boxes.acn" | sort -u)
mapped_types=$(sed -n 's/^[[:space:]]*case \([0-9][0-9]*\)u:.*/\1/p' \
    "$repo/spec/harness/mapping.c" | sort -u)
if [ "$known_types" != "$mapped_types" ]; then
    echo "box-type mapping differs from the ACN choice types" >&2
    exit 1
fi

# The marker codes, box types and brands of lib/hv_codes.h must be the
# values the model states for the field of the same name: `present-when`
# values, fixed INTEGER fields and marker termination patterns. The C names
# the model does not state are listed in `unmodeled`.
{
    sed -En 's/^[[:space:]]*([A-Za-z][A-Za-z0-9]*)[[:space:]]*\[present-when \(?(code|tbox) == ([0-9]+)[),].*/model \2 \1 \3 \3/p' \
        "$repo"/spec/*.acn
    sed -En 's/^[[:space:]]*([A-Za-z][A-Za-z0-9]*)[[:space:]]+INTEGER \(([0-9]+)\.\.([0-9]+)\).*/model code \1 \2 \3/p' \
        "$repo"/spec/*.asn1
    sed -En "s/^[[:space:]]*([A-Za-z][A-Za-z0-9]*)[[:space:]]*\[.*termination-pattern '(FF[0-9A-F]{2})'H.*/model code \1 0x\2 0x\2/p" \
        "$repo"/spec/*.acn
    sed 's/^/c /' "$repo/lib/hv_codes.h"
} | awk '
    function number(s,    n, i) {
        if (s !~ /^0x/)
            return s + 0
        n = 0
        for (i = 3; i <= length(s); i++)
            n = n * 16 + index("0123456789abcdef", tolower(substr(s, i, 1))) - 1
        return n
    }
    BEGIN {
        # Model field names that differ from the C name.
        alias["tilePart"] = "sot"; alias["sizCode"] = "siz"
        alias["segments"] = "eoc"  # a codestream ends at EOC
        alias["headers"] = "sod"   # a tile-part header ends at SOD
        # C names the model does not state.
        split("code:sop code:eph code:no_segment_first code:no_segment_last " \
              "tbox:cgrp tbox:comp tbox:drep tbox:j2cx brand:jp2 brand:jpx", u, " ")
        for (i in u)
            unmodeled[u[i]] = 1
    }
    $1 == "model" && $4 != $5 {
        next                # a range, not a fixed value
    }
    $1 == "model" {
        name = ($3 in alias) ? alias[$3] : tolower($3)
        k = $2 ":" name
        if (index(model[k] " ", " " number($4) " ") == 0)
            model[k] = model[k] " " number($4)
        next
    }
    {
        line = $0
        while (match(line, /HV_[A-Z0-9_]+ = 0x[0-9A-Fa-f]+/)) {
            item = substr(line, RSTART + 3, RLENGTH - 3)
            line = substr(line, RSTART + RLENGTH)
            split(item, p, " = ")
            kind = "code"; name = p[1]
            if (name ~ /^BOX_/) { kind = "tbox"; name = substr(name, 5) }
            else if (name ~ /^BRAND_/) { kind = "brand"; name = substr(name, 7) }
            c[kind ":" tolower(name)] = number(p[2])
        }
    }
    END {
        for (k in c) {
            if (k in unmodeled) {
                if (k in model) { print "hv_codes.h: " k " is in the model" > "/dev/stderr"; bad = 1 }
                continue
            }
            if (!(k in model)) { print "hv_codes.h: " k " is not in the model" > "/dev/stderr"; bad = 1; continue }
            n = split(model[k], v, " ")
            for (i = 1; i <= n; i++)
                if (v[i] != c[k]) {
                    printf "hv_codes.h: %s is %d, the model has %d\n", k, c[k], v[i] > "/dev/stderr"
                    bad = 1
                }
        }
        for (k in unmodeled)
            if (!(k in c)) { print "check-model.sh: unmodeled " k " is not in hv_codes.h" > "/dev/stderr"; bad = 1 }
        exit bad
    }
' || { echo "lib/hv_codes.h differs from the model" >&2; exit 1; }

if [ "$#" -eq 1 ]; then
    corpus=$1
    mkdir -p "$corpus"
    if [ -n "$(find "$corpus" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
        echo "corpus directory is not empty: $corpus" >&2
        exit 2
    fi
    corpus=$(CDPATH= cd -- "$corpus" && pwd)
else
    corpus=$temporary/corpus
    mkdir "$corpus"
fi

docker run --rm \
    -v "$repo:/project:ro" \
    -v "$generated:/output" \
    "$image" \
    -c -ACN --acn-v2 --field-prefix AUTO -o /output \
    /project/spec/j2k-headers.asn1 /project/spec/j2k-headers.acn \
    /project/spec/j2k-codestream.asn1 /project/spec/j2k-codestream.acn \
    /project/spec/jp2-boxes.asn1 /project/spec/jp2-boxes.acn \
    /project/spec/jpeg2000-io.asn1 /project/spec/jpeg2000-io.acn

docker run --rm --entrypoint sh \
    -v "$repo:/project:ro" \
    -v "$generated:/generated" \
    -v "$corpus:/corpus" \
    "$image" -c '
        set -eu
        cc -std=c11 -pedantic-errors -Wall -Wextra -Werror \
            -O1 -g -fsanitize=address,undefined -I/generated -I/project/lib \
            /project/spec/harness/vectors.c \
            /project/spec/harness/crossfield.c \
            /project/spec/harness/mapping.c \
            /project/lib/hv_rules.c \
            /generated/asn1crt.c \
            /generated/asn1crt_encoding.c \
            /generated/asn1crt_encoding_acn.c \
            /generated/asn1crt_encoding_uper.c \
            /generated/j2k-headers.c \
            /generated/j2k-codestream.c \
            /generated/jp2-boxes.c \
            /generated/jpeg2000-io.c \
            -o /tmp/vectors
        /tmp/vectors /corpus
    '

duplicates=$(cut -f1 "$corpus/manifest.tsv" | sort | uniq -d)
if [ -n "$duplicates" ]; then
    echo "duplicate vector names:" >&2
    echo "$duplicates" >&2
    exit 1
fi

echo "model: generated C compiles cleanly as strict C11"
echo "model: corpus labels agree with all mutant expectations"
if [ "$#" -eq 1 ]; then
    echo "model: corpus retained in $corpus"
fi
