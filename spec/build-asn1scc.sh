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
temporary=$(mktemp -d "${TMPDIR:-/tmp}/esajpip-asn1scc.XXXXXX")
source_tree=$temporary/source

cleanup() {
    rm -rf "$temporary"
}
trap cleanup EXIT HUP INT TERM

git -C "$source_repo" cat-file -e "$version^{commit}"
mkdir "$source_tree"
git -C "$source_repo" archive "$version" | tar -x -C "$source_tree"

docker build -f "$repo/spec/Dockerfile.asn1scc" -t "$image" "$source_tree"

docker run --rm --entrypoint sh \
    -v "$repo/spec/asn1scc-issues:/issues:ro" "$image" -c '
    set -eu
    cd /source/v4Tests
    compiler=../asn1scc/bin/Release/net10.0/asn1scc.dll
    for cases in 23-CONTAINING 24-DEDUCED-SIZE 25-ACNV2-BOUNDARIES; do
        ../regression/bin/Release/net10.0/regression \
            -tcd "test-cases/acn/$cases" \
            -ac "$compiler" -l c -s false -acnv2
    done
    ASN1SCC=$compiler ./scripts/runWireTests.sh
    ASN1SCC=$compiler sh ./scripts/runIcdPdusTests.sh
    # Minimal reports exercise the installed compiler too. 0009 covers
    # uPER as well as ACN; the wire suite above generates ACN only.
    export ASN1SCC="dotnet /source/asn1scc/bin/Release/net10.0/asn1scc.dll"
    for issue in /issues/[0-9]*; do
        sh "$issue/run.sh"
    done
'

echo "asn1scc: unmodified pinned upstream compiler built; ACN v2, icdPdus and issue regressions passed"
