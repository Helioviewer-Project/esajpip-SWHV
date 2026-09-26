#!/bin/sh
# Builds and runs the tests of the JPEG 2000 reader/writer and the tools on
# it (hv_transcode, hv_merge), separate from the server's.
#   lib/test/run.sh [normal|sanitize] [CTest options]
# Without a mode, normal: CTest options may come first (run.sh -R merge).
# TRANSCODE_ARCHIVE=dir[:dir...] also transcodes every *.jp2 there.
set -eu

usage="Usage: $0 [normal|sanitize] [CTest options]"
mode=normal
case "${1-}" in
    normal|sanitize) mode=$1; shift ;;
    -h|--help) echo "$usage"; exit 0 ;;
    ''|-*) ;;
    *) echo "$usage" >&2; exit 2 ;;
esac
case "$mode" in
    normal) sanitize=OFF ;;
    sanitize) sanitize=ON ;;
esac

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=${ESAJPIP_TEST_BUILD_DIR:-$repo/build/tool-tests-$mode}

cmake -S "$repo" -B "$build" -DBUILD_TESTING=ON -DESAJPIP_TOOL_TESTS=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DESAJPIP_SANITIZE="$sanitize" >/dev/null
cmake --build "$build" --parallel --target hv_transcode test_transcode test_profile test_writer \
    hv_merge test_merge
exec ctest --test-dir "$build" --output-on-failure -L tools "$@"
