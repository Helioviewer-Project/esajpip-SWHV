#!/bin/sh
# Builds and runs every test of the project: jpeg2000_io and its tools
# (hv_transcode, hv_merge, hv_walk), the transcoder and merger command
# lines, the server, the model's static checks, and, with ESAJPIP_FUZZ=ON,
# the libFuzzer smoke runs.
#
#   tests/run.sh                        the whole suite, normal build
#   tests/run.sh sanitize               the same under ASan and UBSan
#   tests/run.sh normal -L tools        one label (tools, server, cli, model, fuzz)
#   tests/run.sh normal -R transcode    one test
#   tests/run.sh normal -N             list the tests without running them
#
# Environment:
#   ESAJPIP_TEST_BUILD_DIR   the build tree        (default build/tests-MODE)
#   ESAJPIP_TEST_TARGETS     build only these targets, for a quick iteration
#   ESAJPIP_JOBS             build jobs
#   ESAJPIP_CTEST_JOBS       CTest jobs
#   ESAJPIP_FUZZ=ON          also build the fuzz targets (needs libFuzzer)
set -eu

usage() {
    echo "Usage: $0 [normal|sanitize] [CTest options]"
    echo "Examples: $0; $0 sanitize; $0 normal -L tools; $0 normal -R '^merge$'"
    echo "Set ESAJPIP_TEST_BUILD_DIR to override the build directory,"
    echo "ESAJPIP_TEST_TARGETS to build only some targets, and"
    echo "ESAJPIP_FUZZ=ON to include the fuzz targets."
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
. "$repo/tests/run_common.sh"
build=${ESAJPIP_TEST_BUILD_DIR:-$repo/build/tests-$mode}

cmake -S "$repo" -B "$build" -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DESAJPIP_SANITIZE="$sanitize" \
    -DESAJPIP_FUZZ="${ESAJPIP_FUZZ:-OFF}"
if [ -n "${ESAJPIP_TEST_TARGETS:-}" ]; then
    # shellcheck disable=SC2086
    esajpip_build "$build" --target $ESAJPIP_TEST_TARGETS
else
    esajpip_build "$build"
fi
esajpip_ctest "$build" --output-on-failure "$@"
