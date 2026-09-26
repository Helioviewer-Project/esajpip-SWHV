#!/bin/sh
# Regenerates generated/ from the model in spec/: only the reader/writer
# types (asn1scc -icdPdus, the `pdus` list below) and what they depend on.
# A type the reader or writer needs must be added to `pdus`. The modules
# are those of spec/modules, in its order.
#
# The compiler is the pinned build from spec/build-asn1scc.sh. By default it
# runs in its Docker image (ASN1SCC_IMAGE, default esajpip-asn1scc). Set
# ASN1SCC to run a copy of that build (with spec/asn1scc-patches applied)
# directly, as in
#   ASN1SCC="$HOME/jhv/asn1scc-bin/dotnet/dotnet $HOME/jhv/asn1scc-bin/asn1scc/asn1scc.dll"
#
# With --check it only compares: it fails if generated/ is not what the
# compiler makes of the model. spec/check-model.sh runs it so, always with
# the Docker image it builds the corpus with.

set -eu

check=0
case "${1:-}" in
    --check) check=1 ;;
    "") ;;
    *) echo "usage: $0 [--check]" >&2; exit 2 ;;
esac

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=${ASN1SCC_IMAGE:-esajpip-asn1scc}
out=$repo/lib/generated
pdus=BoxHeader,MarkerCode,SegmentLength,SotSegment
pdus=$pdus,CodSegment,QcdSegment,SizFixed,Component,Zplt,Iplt,Rcom
# The served profile (layer 2) types the reader checks decoded values against.
pdus=$pdus,SizFixed-Profile,Component-Profile,MainMarkerCode-Profile,TileMarkerCode-Profile
# JPX boxes.
pdus=$pdus,DataReferenceCount,UrlHeader,FragmentCount,Fragment
pdus=$pdus,RreqHeader,RreqStandardFeature,RreqVendorFeature,FeatureCount
# File Type box.
pdus=$pdus,FtypHeader,Brand
# JP2 header boxes, one box or entry at a time.
pdus=$pdus,Ihdr,BitDepth,ColrHeader,PclrCounts,CmapEntry,CdefCount,CdefEntry,Resolution
models=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$repo/spec/modules")

temporary=$(mktemp -d "${TMPDIR:-/tmp}/hv-generated.XXXXXX")
trap 'rm -rf "$temporary"' EXIT
trap 'exit 1' HUP INT TERM

files=
for m in $models; do
    files="$files spec/$m.asn1 spec/$m.acn"
done

if [ -n "${ASN1SCC:-}" ]; then
    (cd "$repo" && $ASN1SCC -c -ACN --acn-v2 --field-prefix AUTO \
        -icdPdus "$pdus" -o "$temporary" $files) > "$temporary/asn1scc.log" 2>&1 ||
        { cat "$temporary/asn1scc.log" >&2; exit 1; }
else
    docker run --rm -v "$repo:/project:ro" -v "$temporary:/output" -w /project \
        "$image" -c -ACN --acn-v2 --field-prefix AUTO -icdPdus "$pdus" \
        -o /output $files > "$temporary/asn1scc.log" 2>&1 ||
        { cat "$temporary/asn1scc.log" >&2; exit 1; }
fi

if [ "$check" = 1 ]; then
    mkdir "$temporary/generated"
    cp "$temporary"/*.[ch] "$temporary/generated"/
    if ! diff -r "$out" "$temporary/generated" > "$temporary/diff"; then
        echo "lib/generated differs from the model; run lib/generate.sh:" >&2
        head -40 "$temporary/diff" >&2
        exit 1
    fi
    echo "generated: lib/generated is up to date"
    exit 0
fi

mkdir -p "$out"
for f in "$out"/*.[ch]; do
    [ -e "$f" ] || continue
    [ -e "$temporary/$(basename "$f")" ] || rm -f "$f"
done
cp "$temporary"/*.[ch] "$out"/
echo "generated: $(ls "$out" | wc -l | tr -d ' ') files in lib/generated"
