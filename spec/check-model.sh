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
    /project/spec/jp2-boxes.asn1 /project/spec/jp2-boxes.acn

docker run --rm --entrypoint sh \
    -v "$repo:/project:ro" \
    -v "$generated:/generated" \
    -v "$corpus:/corpus" \
    "$image" -c '
        set -eu
        cc -std=c11 -pedantic-errors -Wall -Wextra -Werror \
            -O1 -g -fsanitize=address,undefined -I/generated \
            /project/spec/harness/vectors.c \
            /project/spec/harness/crossfield.c \
            /project/spec/harness/mapping.c \
            /generated/asn1crt.c \
            /generated/asn1crt_encoding.c \
            /generated/asn1crt_encoding_acn.c \
            /generated/asn1crt_encoding_uper.c \
            /generated/j2k-headers.c \
            /generated/j2k-codestream.c \
            /generated/jp2-boxes.c \
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
