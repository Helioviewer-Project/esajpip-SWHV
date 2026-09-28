# Shared runner helpers for the scripts in tests/.
#
# Knobs:
#   ESAJPIP_JOBS          build parallelism for cmake --build
#   ESAJPIP_CTEST_JOBS    CTest parallelism
#
# The standard CMake/CTest variables still work:
#   CMAKE_BUILD_PARALLEL_LEVEL
#   CTEST_PARALLEL_LEVEL

esajpip_build_jobs=${ESAJPIP_JOBS:-${CMAKE_BUILD_PARALLEL_LEVEL:-}}
esajpip_ctest_jobs=${ESAJPIP_CTEST_JOBS:-${CTEST_PARALLEL_LEVEL:-}}

esajpip_build() {
    build_dir=$1
    shift
    if [ -n "$esajpip_build_jobs" ]; then
        cmake --build "$build_dir" --parallel "$esajpip_build_jobs" "$@"
    else
        cmake --build "$build_dir" --parallel "$@"
    fi
}

esajpip_ctest() {
    build_dir=$1
    shift
    if [ -n "$esajpip_ctest_jobs" ]; then
        ctest --test-dir "$build_dir" -j "$esajpip_ctest_jobs" "$@"
    else
        ctest --test-dir "$build_dir" "$@"
    fi
}
