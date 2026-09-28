# hv_transcode: the C port of hvJP2K's transcoder, on the jpeg2000_io
# library in ../lib.

add_executable(hv_transcode hv_transcode.c transcode_file.c transcode.c tier2.c)
esajpip_configure_target(hv_transcode)
target_link_libraries(hv_transcode PRIVATE jpeg2000_io)

# Tests (test/), separate from the server's.
if(ESAJPIP_TOOL_TESTS)
    add_subdirectory(test)
endif()

# fuzz_transcode: a libFuzzer target (Clang only), with ESAJPIP_FUZZ (set up
# in the top-level CMakeLists.txt); fuzz_merge is in ../merge/CMakeLists.txt.
if(ESAJPIP_FUZZ)
    add_executable(fuzz_transcode fuzz_transcode.c transcode.c tier2.c)
    esajpip_configure_target(fuzz_transcode)
    target_compile_options(fuzz_transcode PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_transcode PRIVATE -fsanitize=fuzzer)
    target_link_libraries(fuzz_transcode PRIVATE jpeg2000_io)
endif()
