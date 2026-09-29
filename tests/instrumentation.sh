# Instrumentation profiles that are not build options, kept with the runs
# that need them: the suite asks for them, so the suite owns them.
#
#   . tests/instrumentation.sh
#   esajpip_instrumentation extended
#   cmake -B build -DCMAKE_C_FLAGS="$ESAJPIP_CFLAGS" \
#       -DCMAKE_CXX_FLAGS="$ESAJPIP_CXXFLAGS" \
#       -DCMAKE_EXE_LINKER_FLAGS="$ESAJPIP_LDFLAGS" $ESAJPIP_CMAKE_EXTRA
#
# Profiles:
#   extended   asan and ubsan plus integer and bounds checks
#   msan       MemorySanitizer with origins (Linux: the Darwin target lacks it)
#   coverage   Clang source-based coverage (tests/run_baseline.sh)
#   fuzz       the targets carry their own flags (tests/fuzz); LTO must go
#   none       nothing
#
# asan is not here: ESAJPIP_SANITIZE is the build option for it.

esajpip_instrumentation() {
    ESAJPIP_CFLAGS=
    ESAJPIP_CXXFLAGS=
    ESAJPIP_LDFLAGS=
    ESAJPIP_CMAKE_EXTRA=

    case "${1:-none}" in
    none)
        ;;
    extended)
        # LTO and sanitizer instrumentation do not mix.
        ESAJPIP_CMAKE_EXTRA=-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
        esa_flags="-fsanitize=address,undefined,unsigned-integer-overflow,implicit-conversion,local-bounds"
        ESAJPIP_CFLAGS="$esa_flags -fno-sanitize-recover=all -fno-omit-frame-pointer -UNDEBUG"
        ESAJPIP_CXXFLAGS=$ESAJPIP_CFLAGS
        ESAJPIP_LDFLAGS="$esa_flags -fno-sanitize-recover=all"
        ;;
    msan)
        ESAJPIP_CMAKE_EXTRA=-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
        ESAJPIP_CFLAGS="-fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer -UNDEBUG"
        ESAJPIP_CXXFLAGS=$ESAJPIP_CFLAGS
        ESAJPIP_LDFLAGS="-fsanitize=memory -fsanitize-memory-track-origins=2"
        ;;
    coverage)
        ESAJPIP_CMAKE_EXTRA=-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
        ESAJPIP_CFLAGS="-fprofile-instr-generate -fcoverage-mapping -fno-omit-frame-pointer"
        ESAJPIP_CXXFLAGS="$ESAJPIP_CFLAGS -DESAJPIP_COVERAGE"
        ESAJPIP_LDFLAGS="-fprofile-instr-generate"
        ;;
    fuzz)
        ESAJPIP_CMAKE_EXTRA=-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
        ;;
    *)
        echo "instrumentation.sh: unknown profile $1" >&2
        return 2
        ;;
    esac
}
