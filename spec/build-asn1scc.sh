#!/bin/sh

set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 ASN1SCC-SOURCE [IMAGE]" >&2
    exit 2
fi

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_repo=$1
image=${2:-esajpip-asn1scc}
version=$(cat "$repo/spec/VERSION")
patch_dir=$repo/spec/asn1scc-patches
temporary=$(mktemp -d "${TMPDIR:-/tmp}/esajpip-asn1scc.XXXXXX")
source_tree=$temporary/source

cleanup() {
    rm -rf "$temporary"
}
trap cleanup EXIT HUP INT TERM

git -C "$source_repo" cat-file -e "$version^{commit}"
mkdir "$source_tree"
git -C "$source_repo" archive "$version" | tar -x -C "$source_tree"

# Fixes absent from the pinned upstream revision, in series order.
while IFS= read -r patch_name; do
    [ -n "$patch_name" ] || continue
    patch -s -d "$source_tree" -p1 < "$patch_dir/$patch_name"
done < "$patch_dir/series"

docker build -f "$repo/spec/Dockerfile.asn1scc" -t "$image" "$source_tree"

docker run --rm --entrypoint sh "$image" -c '
    set -eu
    cd /source/v4Tests
    compiler=../asn1scc/bin/Release/net10.0/asn1scc.dll
    ../regression/bin/Release/net10.0/regression \
        -tcd test-cases/acn/25-ACNV2-BOUNDARIES \
        -ac "$compiler" -l c -s false -acnv2
    ASN1SCC=$compiler ./scripts/runWireTests.sh
    ASN1SCC=$compiler sh ./scripts/runIcdPdusTests.sh
'

echo "asn1scc: pinned upstream compiler with local patches built and ACN v2 regressions passed"
