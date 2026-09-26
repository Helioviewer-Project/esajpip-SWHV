# The transcoder's tests, separate from the server's: built and registered
# only with ESAJPIP_TOOL_TESTS, labelled "tools". Run them with
# lib/test/run.sh.

add_executable(test_transcode test_transcode.c ../transcode_file.c ../transcode.c ../tier2.c)
esajpip_configure_target(test_transcode)
target_include_directories(test_transcode PRIVATE ..)
target_compile_definitions(test_transcode PRIVATE
    TRANSCODE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(test_transcode PRIVATE jpeg2000_io)
add_test(NAME transcode COMMAND test_transcode)
# Any sanitizer report fails the test.
set(TRANSCODE_TEST_ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
set_tests_properties(transcode PROPERTIES LABELS tools
    ENVIRONMENT "${TRANSCODE_TEST_ENVIRONMENT}")

add_test(NAME transcode_command
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/cli_test.sh $<TARGET_FILE:hv_transcode>
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures ${CMAKE_CURRENT_BINARY_DIR}/command)
set_tests_properties(transcode_command PROPERTIES LABELS tools
    ENVIRONMENT "${TRANSCODE_TEST_ENVIRONMENT}")
