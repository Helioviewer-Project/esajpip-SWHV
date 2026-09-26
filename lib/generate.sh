#!/bin/sh
# Regenerates generated/ from the model in spec/: only the reader/writer
# types (asn1scc -icdPdus) and what they depend on.
#
# The compiler is the pinned build from spec/build-asn1scc.sh. By default it
# runs in its Docker image, as spec/check-model.sh does. Set ASN1SCC to run
# a copy of that build (with spec/asn1scc-patches applied) directly, for
# example:
#   ASN1SCC="$HOME/jhv/asn1scc-bin/dotnet/dotnet $HOME/jhv/asn1scc-bin/asn1scc/asn1scc.dll"

set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=${ASN1SCC_IMAGE:-esajpip-asn1scc}
out=$repo/lib/generated
pdus=BoxHeader,MarkerCode,SegmentLength,SotSegment
pdus=$pdus,SizSegment-Std,CodSegment-Std,QcdSegment-Std,PltSegment-Std,ComSegment-Std
# The served profile (layer 2) types the reader checks decoded values against.
pdus=$pdus,Siz-Profile,MainMarkerCode-Profile,TileMarkerCode-Profile
# JPX boxes.
pdus=$pdus,DataReferenceCount,UrlHeader,FragmentCount,Fragment,FragmentList-Profile,Rreq-Std
# File Type box.
pdus=$pdus,FtypHeader,Brand
# JP2 header boxes, one box or entry at a time.
pdus=$pdus,Ihdr,BitDepth,ColrHeader,PclrHeader,CmapEntry,CdefCount,CdefEntry,Resolution
models="j2k-headers j2k-codestream jp2-boxes jpeg2000-io"

temporary=$(mktemp -d "${TMPDIR:-/tmp}/hv-generated.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

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

mkdir -p "$out"
for f in "$out"/*.[ch]; do
    [ -e "$f" ] || continue
    [ -e "$temporary/$(basename "$f")" ] || rm -f "$f"
done
cp "$temporary"/*.[ch] "$out"/
echo "generated: $(ls "$out" | wc -l | tr -d ' ') files in lib/generated"
