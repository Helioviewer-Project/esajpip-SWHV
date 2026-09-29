#!/bin/sh
# Linux-only diagnostics in a Debian 13 container: deterministic replay under
# Valgrind and MemorySanitizer, plus optional libFuzzer mutation campaigns
# under Linux sanitizer runtimes. The repository is mounted read-only; build
# trees stay in the disposable container. Fuzz corpora, logs and artifacts
# remain in a separate host directory for each run (ESAJPIP_LINUX_OUT).
#
#   tests/run_linux_docker.sh valgrind|msan|all
#   tests/run_linux_docker.sh fuzz-asan|fuzz-extended|fuzz-msan|fuzz-all
#
# See tests/DIAGNOSTICS.md.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
profile=${1:-all}
platform=${ESAJPIP_DOCKER_PLATFORM:-}
image=${ESAJPIP_DOCKER_IMAGE:-esajpip-linux-diagnostics:debian13}

usage() {
    echo "Usage: $0 [valgrind|msan|all|fuzz-asan|fuzz-extended|fuzz-msan|fuzz-all]" >&2
    echo "Set ESAJPIP_DOCKER_PLATFORM=linux/arm64 or linux/amd64 to pin architecture." >&2
    echo "Set ESAJPIP_JOBS for Docker build jobs." >&2
    echo "Set ESAJPIP_LINUX_OUT for the parent of persistent result directories." >&2
    echo "Set ESAJPIP_FUZZ_SECONDS, ESAJPIP_FUZZ_WORKERS and ESAJPIP_FUZZ_TARGETS for Docker fuzz runs." >&2
}

case "$profile" in
    valgrind|msan|all|fuzz-asan|fuzz-extended|fuzz-msan|fuzz-all)
        ;;
    *)
        usage
        exit 2
        ;;
esac

docker_platform=
if [ -n "$platform" ]; then
    docker_platform="--platform $platform"
fi

docker build $docker_platform -f "$repo/tests/Dockerfile.linux" -t "$image" "$repo"

out=${ESAJPIP_LINUX_OUT:-$repo/build/linux-diagnostics}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
results=$(mktemp -d "$out/$profile.XXXXXX")
echo "Linux diagnostic results: $results"

docker run --rm -i $docker_platform -v "$repo:/src:ro" -v "$results:/results" -w /src \
    -e ESAJPIP_LINUX_PROFILE="$profile" \
    -e ESAJPIP_JOBS="${ESAJPIP_JOBS:-${CMAKE_BUILD_PARALLEL_LEVEL:-}}" \
    -e ESAJPIP_FUZZ_SECONDS="${ESAJPIP_FUZZ_SECONDS:-60}" \
    -e ESAJPIP_FUZZ_WORKERS="${ESAJPIP_FUZZ_WORKERS:-8}" \
    -e ESAJPIP_FUZZ_TARGETS="${ESAJPIP_FUZZ_TARGETS:-}" \
    "$image" sh -s <<'EOF'
set -eu

profile=${ESAJPIP_LINUX_PROFILE:-all}
fuzz_seconds=${ESAJPIP_FUZZ_SECONDS:-60}
fuzz_workers=${ESAJPIP_FUZZ_WORKERS:-8}
fuzz_targets=${ESAJPIP_FUZZ_TARGETS:-"fuzz_reader_rewrite fuzz_deferred_plt fuzz_asn1 fuzz_transcode fuzz_merge"}
build_jobs=${ESAJPIP_JOBS:-}

build_parallel() {
    build_dir=$1
    shift
    if [ -n "$build_jobs" ]; then
        cmake --build "$build_dir" --parallel "$build_jobs" "$@"
    else
        cmake --build "$build_dir" --parallel "$@"
    fi
}

# Seeds in the two shapes the replay modes take: a raw codestream (the jp2c
# payload of a fixture) and the merge target's 4-byte split followed by two
# JP2 files.
mkdir -p /tmp/esajpip-seeds
python3 - <<'PY'
from pathlib import Path
import struct

root = Path("/src")
seed_dir = Path("/tmp/esajpip-seeds")

def jp2c_payload(path):
    data = path.read_bytes()
    pos = 0
    while pos + 8 <= len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        box_type = data[pos + 4:pos + 8]
        header = 8
        if length == 1:
            if pos + 16 > len(data):
                break
            length = int.from_bytes(data[pos + 8:pos + 16], "big")
            header = 16
        elif length == 0:
            length = len(data) - pos
        if length < header or pos + length > len(data):
            break
        if box_type == b"jp2c":
            return data[pos + header:pos + length]
        pos += length
    raise SystemExit(f"jp2c box not found in {path}")

fixtures = root / "tests/transcode/fixtures/input"
codestream_source = fixtures / "solo_fsi174_511x513_LRCP_PLT.jp2"
(seed_dir / "codestream.j2k").write_bytes(jp2c_payload(codestream_source))

first = (fixtures / "solo_fsi174_511x513_LRCP_PLT.jp2").read_bytes()
second = (fixtures / "solo_fsi174_510x514_LRCP.jp2").read_bytes()
(seed_dir / "merge-fuzz.seed").write_bytes(struct.pack(">I", len(first)) + first + second)
PY

# Every replay mode once, on the corpus and on the two seeds. The modes are
# the fuzz targets: see tests/fuzz/CMakeLists.txt.
run_matrix() {
    asn1_corpus=$1
    shift
    runner=$1
    shift

    "$runner" "$@" reader-rewrite /src/tests/vectors/j2k/jp2.jp2
    "$runner" "$@" reader-rewrite /src/tests/vectors/j2k/jp2-precincts.jp2
    "$runner" "$@" reader-rewrite \
        /src/tests/transcode/fixtures/input/solo_fsi174_511x513_LRCP_PLT.jp2
    "$runner" "$@" deferred-plt /src/tests/vectors/j2k/jp2-precincts.jp2
    "$runner" "$@" transcode-fuzz /tmp/esajpip-seeds/codestream.j2k
    "$runner" "$@" merge-fuzz /tmp/esajpip-seeds/merge-fuzz.seed
    "$runner" "$@" asn1 "$asn1_corpus"
}

fuzz_corpus() {
    case "$1" in
        fuzz_reader_rewrite) echo reader-rewrite ;;
        fuzz_deferred_plt) echo deferred-plt ;;
        fuzz_asn1) echo asn1 ;;
        fuzz_transcode) echo transcode-fuzz ;;
        fuzz_merge) echo merge-fuzz ;;
        *)
            echo "unknown fuzz target: $1" >&2
            return 2
            ;;
    esac
}

run_fuzz_profile() {
    name=$1
    build=$2
    shift 2

    # shellcheck disable=SC2086
    cmake -S /src -B "$build" \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DBUILD_TESTING=ON \
        -DESAJPIP_FUZZ=ON \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
        "$@" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
    # shellcheck disable=SC2086
    if [ -n "$build_jobs" ]; then
        # shellcheck disable=SC2086
        cmake --build "$build" --parallel "$build_jobs" --target $fuzz_targets
    else
        # shellcheck disable=SC2086
        cmake --build "$build" --parallel "$fuzz_workers" --target $fuzz_targets
    fi

    for target in $fuzz_targets; do
        corpus=$(fuzz_corpus "$target")
        run_dir="/results/$name/$target"
        corpus_dir="$run_dir/corpus"
        artifact_dir="$run_dir/artifacts"
        log="$run_dir/run.log"

        mkdir -p "$artifact_dir" "$corpus_dir"
        cp -R "$build/tests/fuzz/corpus/$corpus/." "$corpus_dir/"
        echo "Running $target under $name for ${fuzz_seconds}s with ${fuzz_workers} workers"
        if ! (cd "$run_dir" && "$build/tests/fuzz/$target" \
            -max_total_time="$fuzz_seconds" \
            -jobs="$fuzz_workers" \
            -workers="$fuzz_workers" \
            -print_final_stats=1 \
            -artifact_prefix="$artifact_dir/" \
            "$corpus_dir") >"$log" 2>&1; then
            cat "$log" >&2
            exit 1
        fi
        grep -E '^(Done |stat::number_of_executed_units|stat::average_exec_per_sec|stat::new_units_added|stat::peak_rss_mb)' "$log" || tail -n 20 "$log"
        artifacts=$(find "$artifact_dir" -type f | wc -l)
        echo "$target artifacts: $artifacts"
    done
}

if [ "$profile" = valgrind ] || [ "$profile" = all ]; then
    cmake -S /src -B /tmp/esajpip-linux-valgrind \
        -DBUILD_TESTING=ON \
        -DCMAKE_BUILD_TYPE=Debug
    build_parallel /tmp/esajpip-linux-valgrind --target replay support_test jpip_tests
    ctest --test-dir /tmp/esajpip-linux-valgrind --output-on-failure -L '^jpip$'
    /tmp/esajpip-linux-valgrind/tests/server/support_test
    run_matrix /tmp/esajpip-linux-valgrind/tests/fuzz/corpus/asn1 valgrind \
        --tool=memcheck \
        --leak-check=full \
        --show-leak-kinds=all \
        --track-origins=yes \
        --error-exitcode=99 \
        /tmp/esajpip-linux-valgrind/tests/fuzz/replay
fi

if [ "$profile" = msan ] || [ "$profile" = all ]; then
    . /src/tests/instrumentation.sh
    esajpip_instrumentation msan
    # shellcheck disable=SC2086
    cmake -S /src -B /tmp/esajpip-linux-msan \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DBUILD_TESTING=ON \
        -DCMAKE_C_FLAGS="$ESAJPIP_CFLAGS" \
        -DCMAKE_CXX_FLAGS="$ESAJPIP_CXXFLAGS" \
        -DCMAKE_EXE_LINKER_FLAGS="$ESAJPIP_LDFLAGS" \
        $ESAJPIP_CMAKE_EXTRA \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
    build_parallel /tmp/esajpip-linux-msan --target replay
    export MSAN_OPTIONS=halt_on_error=1:exit_code=126
    run_matrix /tmp/esajpip-linux-msan/tests/fuzz/corpus/asn1 \
        /tmp/esajpip-linux-msan/tests/fuzz/replay
fi

if [ "$profile" = fuzz-asan ] || [ "$profile" = fuzz-all ]; then
    run_fuzz_profile asan /tmp/esajpip-linux-fuzz-asan \
        -DESAJPIP_SANITIZE=ON
fi

if [ "$profile" = fuzz-extended ] || [ "$profile" = fuzz-all ]; then
    . /src/tests/instrumentation.sh
    esajpip_instrumentation extended
    # shellcheck disable=SC2086
    run_fuzz_profile extended /tmp/esajpip-linux-fuzz-extended \
        -DCMAKE_C_FLAGS="$ESAJPIP_CFLAGS" \
        -DCMAKE_CXX_FLAGS="$ESAJPIP_CXXFLAGS" \
        -DCMAKE_EXE_LINKER_FLAGS="$ESAJPIP_LDFLAGS" \
        $ESAJPIP_CMAKE_EXTRA
fi

if [ "$profile" = fuzz-msan ] || [ "$profile" = fuzz-all ]; then
    . /src/tests/instrumentation.sh
    esajpip_instrumentation msan
    export MSAN_OPTIONS=halt_on_error=1:exit_code=126
    # shellcheck disable=SC2086
    run_fuzz_profile msan /tmp/esajpip-linux-fuzz-msan \
        -DCMAKE_C_FLAGS="$ESAJPIP_CFLAGS" \
        -DCMAKE_CXX_FLAGS="$ESAJPIP_CXXFLAGS" \
        -DCMAKE_EXE_LINKER_FLAGS="$ESAJPIP_LDFLAGS" \
        $ESAJPIP_CMAKE_EXTRA
fi
EOF
