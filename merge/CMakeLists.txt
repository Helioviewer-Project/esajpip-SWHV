# hv_merge: the C port of hvJP2K's JPX merger, on the jpeg2000_io
# library in ../lib.

add_executable(hv_merge hv_merge.c merge.c)
esajpip_configure_target(hv_merge)
target_link_libraries(hv_merge PRIVATE jpeg2000_io)

# Tests (test/), separate from the server's.
if(ESAJPIP_TOOL_TESTS)
    add_subdirectory(test)
endif()

# fuzz_merge: a libFuzzer target (Clang only), with ESAJPIP_FUZZ
# (../transcode/CMakeLists.txt).
if(ESAJPIP_FUZZ)
    add_executable(fuzz_merge fuzz_merge.c merge.c)
    esajpip_configure_target(fuzz_merge)
    target_compile_options(fuzz_merge PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_merge PRIVATE -fsanitize=fuzzer)
    target_link_libraries(fuzz_merge PRIVATE jpeg2000_io)
endif()
