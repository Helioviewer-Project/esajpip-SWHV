# The merger's tests, with the reader's and the transcoder's and separate
# from the server's: built and registered only with ESAJPIP_TOOL_TESTS,
# labelled "tools". Run them with lib/test/run.sh.

set(MERGE_TRANSCODE_FIXTURES ${PROJECT_SOURCE_DIR}/transcode/test/fixtures)
add_executable(test_merge test_merge.c ../merge.c)
esajpip_configure_target(test_merge)
target_include_directories(test_merge PRIVATE ..)
target_compile_definitions(test_merge PRIVATE
    MERGE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    TRANSCODE_FIXTURES="${MERGE_TRANSCODE_FIXTURES}")
target_link_libraries(test_merge PRIVATE jpeg2000_io)
add_test(NAME merge COMMAND test_merge)
set(MERGE_TEST_ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
set_tests_properties(merge PROPERTIES LABELS tools ENVIRONMENT "${MERGE_TEST_ENVIRONMENT}")

add_test(NAME merge_command
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/cli_test.sh $<TARGET_FILE:hv_merge>
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures ${MERGE_TRANSCODE_FIXTURES}
        ${CMAKE_CURRENT_BINARY_DIR}/command)
set_tests_properties(merge_command PROPERTIES LABELS tools
    ENVIRONMENT "${MERGE_TEST_ENVIRONMENT}")
