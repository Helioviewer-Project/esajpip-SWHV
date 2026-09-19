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

while IFS= read -r patch_name; do
    [ -n "$patch_name" ] || continue
    patch -s -d "$source_tree" -p1 < "$patch_dir/$patch_name"
done < "$patch_dir/series"

docker build -f "$repo/spec/Dockerfile.asn1scc" -t "$image" "$source_tree"

docker run --rm --entrypoint sh "$image" -c \
    'ASN1SCC=/source/asn1scc/bin/Release/net10.0/asn1scc.dll sh /source/v4Tests/scripts/runDeferredAcnRegressions.sh'

echo "asn1scc: compiler built and deferred-ACN regressions passed"
