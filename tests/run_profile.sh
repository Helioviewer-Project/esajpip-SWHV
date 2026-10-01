#!/bin/sh
# Build and run one diagnostic profile of the test suite: the same tests as
# tests/run.sh, under another sanitizer or toolchain. Fuzz and Valgrind are
# explicit because they need toolchain or runtime support that is not on
# every host.
#
#   tests/run_profile.sh asan|extended|msan|optimized
#   tests/run_profile.sh fuzz [CTest options] (needs the libFuzzer runtime)
#   tests/run_profile.sh valgrind [replay files...]
#
# Builds go to build/profile-MODE, or ESAJPIP_TEST_BUILD_DIR.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$repo/tests/run_common.sh"
mode=${1:-}
shift || true
. "$repo/tests/instrumentation.sh"

usage() {
    echo "Usage: $0 asan|extended|msan|optimized|fuzz|valgrind [replay files...]" >&2
}

# The whole suite under one profile. `profile` is an instrumentation.sh name
# (asan additionally passes the build's own ESAJPIP_SANITIZE).
run_ctest_profile() {
    build=$1
    build_type=$2
    profile=$3
    shift 3
    esajpip_instrumentation "$profile"
    # shellcheck disable=SC2086
    cmake -S "$repo" -B "$build" \
        -DBUILD_TESTING=ON \
        -DCMAKE_C_FLAGS="$ESAJPIP_CFLAGS" \
        -DCMAKE_CXX_FLAGS="$ESAJPIP_CXXFLAGS" \
        -DCMAKE_EXE_LINKER_FLAGS="$ESAJPIP_LDFLAGS" \
        $ESAJPIP_CMAKE_EXTRA \
        "$@" \
        -DCMAKE_BUILD_TYPE="$build_type"
    esajpip_build "$build"
    esajpip_ctest "$build" --output-on-failure
}

case "$mode" in
    asan)
        run_ctest_profile "${ESAJPIP_TEST_BUILD_DIR:-$repo/build/profile-asan}" \
            RelWithDebInfo none -DESAJPIP_SANITIZE=ON
        ;;
    extended)
        run_ctest_profile "${ESAJPIP_TEST_BUILD_DIR:-$repo/build/profile-extended}" \
            RelWithDebInfo extended
        ;;
    msan)
        run_ctest_profile "${ESAJPIP_TEST_BUILD_DIR:-$repo/build/profile-msan}" \
            RelWithDebInfo msan
        ;;
    optimized)
        run_ctest_profile "${ESAJPIP_TEST_BUILD_DIR:-$repo/build/profile-optimized}" \
            Release none
        ;;
    fuzz)
        # The fuzz targets need Clang's libFuzzer runtime: Homebrew's LLVM has
        # it, the Apple Command Line Tools do not.
        build=${ESAJPIP_TEST_BUILD_DIR:-$repo/build/profile-fuzz}
        llvm_prefix=${LLVM_PREFIX:-}
        if [ -z "$llvm_prefix" ] && command -v brew >/dev/null 2>&1; then
            llvm_prefix=$(brew --prefix llvm 2>/dev/null || true)
        fi
        cc=${CC:-clang}
        cxx=${CXX:-clang++}
        if [ -n "$llvm_prefix" ]; then
            cc=$llvm_prefix/bin/clang
            cxx=$llvm_prefix/bin/clang++
        fi
        # LTO off: the fuzz targets carry the coverage runtime.
        esajpip_instrumentation fuzz
        # shellcheck disable=SC2086
        cmake -S "$repo" -B "$build" \
            -DBUILD_TESTING=ON \
            -DESAJPIP_SANITIZE=ON \
            -DESAJPIP_FUZZ=ON \
            -DCMAKE_C_COMPILER="$cc" \
            -DCMAKE_CXX_COMPILER="$cxx" \
            $ESAJPIP_CMAKE_EXTRA \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo
        esajpip_build "$build"
        # A short deterministic run of every target. For a campaign, point a
        # target at a corpus of its own: tests/vectors/j2k and
        # tests/transcode/fixtures are already useful input.
        esajpip_ctest "$build" --output-on-failure -L fuzz "$@"
        ;;
    valgrind)
        if ! command -v valgrind >/dev/null 2>&1; then
            echo "valgrind is not installed on this host" >&2
            exit 127
        fi
        build=${ESAJPIP_TEST_BUILD_DIR:-$repo/build/profile-valgrind}
        cmake -S "$repo" -B "$build" -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
        esajpip_build "$build" --target replay
        if [ "$#" -eq 0 ]; then
            set -- "$repo/tests/vectors/j2k/jp2.jp2" "$repo/tests/vectors/j2k/jp2-precincts.jp2"
        fi
        valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
            --track-origins=yes --error-exitcode=99 \
            "$build/tests/fuzz/replay" reader-rewrite "$@"
        ;;
    *)
        usage
        exit 2
        ;;
esac
