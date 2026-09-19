#!/bin/sh
set -eu

usage() {
    echo "Usage: $0 [normal|sanitize] [CTest options]"
    echo "Examples: $0; $0 sanitize; $0 normal -R '^server$'"
    echo "Set ESAJPIP_TEST_BUILD_DIR to override the build directory."
}

mode=${1:-normal}
case "$mode" in
    normal) sanitize=OFF ;;
    sanitize) sanitize=ON ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac
if [ "$#" -gt 0 ]; then shift; fi

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${ESAJPIP_TEST_BUILD_DIR:-$repo/build/tests-$mode}

cmake -S "$repo" -B "$build" -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DESAJPIP_SANITIZE="$sanitize"
cmake --build "$build" --parallel
exec ctest --test-dir "$build" --output-on-failure "$@"
