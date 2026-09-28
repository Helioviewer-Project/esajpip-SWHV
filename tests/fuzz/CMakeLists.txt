# The fuzz targets of jpeg2000_io, hv_transcode and hv_merge, their seed
# corpora, and their deterministic counterpart.
#
# `replay` includes every target under another name, so a replay mode *is* a
# target and the two cannot drift. It needs no libFuzzer runtime, is built
# always, and is what the sanitizer, coverage and Valgrind runs use:
#
#   build/tests-*/tests/fuzz/replay reader-rewrite FILE_OR_DIR...
#   build/tests-*/tests/fuzz/replay deferred-plt FILE_OR_DIR...
#   build/tests-*/tests/fuzz/replay asn1 FILE_OR_DIR...
#   build/tests-*/tests/fuzz/replay transcode-fuzz FILE_OR_DIR...
#   build/tests-*/tests/fuzz/replay merge-fuzz FILE_OR_DIR...
#
# The targets themselves need Clang's libFuzzer runtime (ESAJPIP_FUZZ=ON,
# checked in the top-level CMakeLists.txt). Each gets a short deterministic
# run in CTest, so a crash stays reproducible from an ordinary test run:
#
#   ctest --test-dir build -L fuzz
#
find_package(Python3 COMPONENTS Interpreter QUIET)
set(ESAJPIP_FUZZ_CORPUS_DIR ${CMAKE_CURRENT_BINARY_DIR}/corpus)
if(Python3_Interpreter_FOUND)
    file(GLOB_RECURSE ESAJPIP_FUZZ_CORPUS_INPUTS CONFIGURE_DEPENDS
        ${PROJECT_SOURCE_DIR}/tests/vectors/j2k/*.jp2
        ${PROJECT_SOURCE_DIR}/tests/vectors/j2k/*.jpx
        ${PROJECT_SOURCE_DIR}/tests/transcode/fixtures/input/*.jp2
        ${PROJECT_SOURCE_DIR}/tests/merge/fixtures/input/*.jp2)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/corpus.stamp
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/make_corpus.py
                ${PROJECT_SOURCE_DIR}
                ${ESAJPIP_FUZZ_CORPUS_DIR}
        COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/corpus.stamp
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/make_corpus.py
            ${ESAJPIP_FUZZ_CORPUS_INPUTS})
    add_custom_target(fuzz_corpus DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/corpus.stamp)
else()
    message(WARNING "Python3 not found: fuzz corpus replay tests will not be registered")
endif()

set(ESAJPIP_FUZZ_SOURCES
    ${PROJECT_SOURCE_DIR}/transcode/transcode.c
    ${PROJECT_SOURCE_DIR}/transcode/tier2.c
    ${PROJECT_SOURCE_DIR}/merge/merge.c)
set(ESAJPIP_FUZZ_INCLUDES
    ${PROJECT_SOURCE_DIR}/transcode
    ${PROJECT_SOURCE_DIR}/merge)

# The deterministic driver: no libFuzzer, so it builds in any configuration.
add_executable(replay replay.c ${ESAJPIP_FUZZ_SOURCES})
esajpip_configure_target(replay)
target_include_directories(replay PRIVATE ${ESAJPIP_FUZZ_INCLUDES})
target_link_libraries(replay PRIVATE jpeg2000_io)
if(TARGET fuzz_corpus)
    add_dependencies(replay fuzz_corpus)

    esajpip_add_test(fuzz_replay_reader_rewrite
        COMMAND replay reader-rewrite ${ESAJPIP_FUZZ_CORPUS_DIR}/reader-rewrite
        LABELS fuzz replay)
    esajpip_add_test(fuzz_replay_deferred_plt
        COMMAND replay deferred-plt ${ESAJPIP_FUZZ_CORPUS_DIR}/deferred-plt
        LABELS fuzz replay)
    esajpip_add_test(fuzz_replay_asn1
        COMMAND replay asn1 ${ESAJPIP_FUZZ_CORPUS_DIR}/asn1
        LABELS fuzz replay)
    esajpip_add_test(fuzz_replay_transcode
        COMMAND replay transcode-fuzz ${ESAJPIP_FUZZ_CORPUS_DIR}/transcode-fuzz
        LABELS fuzz replay)
    esajpip_add_test(fuzz_replay_merge
        COMMAND replay merge-fuzz ${ESAJPIP_FUZZ_CORPUS_DIR}/merge-fuzz
        LABELS fuzz replay)
endif()

if(NOT ESAJPIP_FUZZ)
    # The replay driver above needs nothing else.
    return()
endif()

# libFuzzer is a Clang component of its own: Homebrew's LLVM has it, the
# Apple Command Line Tools do not. One message here beats a linker error per
# target.
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "ESAJPIP_FUZZ requires Clang")
endif()
set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
check_c_source_compiles(
    "int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long n)
     { (void)d; (void)n; return 0; }"
    ESAJPIP_LIBFUZZER_SUPPORTED)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT ESAJPIP_LIBFUZZER_SUPPORTED)
    message(FATAL_ERROR
        "ESAJPIP_FUZZ needs the libFuzzer runtime, which ${CMAKE_C_COMPILER} does\n"
        "not provide. Homebrew's LLVM does:\n"
        "  sh tests/run_profile.sh fuzz\n"
        "The replay driver needs none of it: build/tests/fuzz/replay")
endif()

# Instrument the library too, so the targets' coverage sees into it, and give
# everything that links it the coverage runtime.
target_compile_options(jpeg2000_io PRIVATE -fsanitize=fuzzer-no-link)
target_link_options(jpeg2000_io INTERFACE -fsanitize=fuzzer-no-link)

# esajpip_fuzz_target(<name> <corpus> <sources...>): the shared part of a target.
function(esajpip_fuzz_target name corpus)
    add_executable(${name} ${ARGN})
    esajpip_configure_target(${name})
    target_include_directories(${name} PRIVATE ${ESAJPIP_FUZZ_INCLUDES})
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_libraries(${name} PRIVATE jpeg2000_io)
    if(TARGET fuzz_corpus)
        add_dependencies(${name} fuzz_corpus)
        esajpip_add_test(fuzz_smoke_${name}
            COMMAND ${name} -runs=20000 -seed=1 -max_len=4096
                    ${ESAJPIP_FUZZ_CORPUS_DIR}/${corpus}
            LABELS fuzz)
    else()
        esajpip_add_test(fuzz_smoke_${name}
            COMMAND ${name} -runs=20000 -seed=1 -max_len=4096
            LABELS fuzz)
    endif()
endfunction()

# The library: the container reader and writer, deferred PLT consumption, and
# the generated ASN.1 decoders.
esajpip_fuzz_target(fuzz_reader_rewrite reader-rewrite fuzz_reader_rewrite.c)
esajpip_fuzz_target(fuzz_deferred_plt deferred-plt fuzz_deferred_plt.c)
esajpip_fuzz_target(fuzz_asn1 asn1 fuzz_asn1.c)

# The tools: a transcoded codestream is idempotent and in the served profile,
# and a merged file reads back.
esajpip_fuzz_target(fuzz_transcode transcode-fuzz fuzz_transcode.c
    ${PROJECT_SOURCE_DIR}/transcode/transcode.c
    ${PROJECT_SOURCE_DIR}/transcode/tier2.c)
esajpip_fuzz_target(fuzz_merge merge-fuzz fuzz_merge.c
    ${PROJECT_SOURCE_DIR}/merge/merge.c)
