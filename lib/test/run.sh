#!/bin/sh
# Builds and runs the tests of the JPEG 2000 reader/writer and the tools on
# it (hv_transcode, hv_merge), separate from the server's.
#   lib/test/run.sh [normal|sanitize] [CTest options]
# TRANSCODE_ARCHIVE=dir[:dir...] also transcodes every *.jp2 there.
set -eu

mode=${1:-normal}
case "$mode" in
    normal) sanitize=OFF ;;
    sanitize) sanitize=ON ;;
    -h|--help) echo "Usage: $0 [normal|sanitize] [CTest options]"; exit 0 ;;
    *) echo "Usage: $0 [normal|sanitize] [CTest options]" >&2; exit 2 ;;
esac
if [ "$#" -gt 0 ]; then shift; fi

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=${ESAJPIP_TEST_BUILD_DIR:-$repo/build/tool-tests-$mode}

cmake -S "$repo" -B "$build" -DBUILD_TESTING=ON -DESAJPIP_TOOL_TESTS=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DESAJPIP_SANITIZE="$sanitize" >/dev/null
cmake --build "$build" --parallel --target hv_transcode test_transcode test_profile \
    hv_merge test_merge
exec ctest --test-dir "$build" --output-on-failure -L tools "$@"
