# The reader's tests, with hv_transcode's and separate from the server's:
# built and registered only with ESAJPIP_TOOL_TESTS, labeled "tools". Run
# them with lib/test/run.sh.

add_executable(test_profile test_profile.c)
esajpip_configure_target(test_profile)
target_link_libraries(test_profile PRIVATE jpeg2000_io)
add_test(NAME reader_profile
    COMMAND test_profile ${PROJECT_SOURCE_DIR}/tests/vectors/j2k)
set_tests_properties(reader_profile PROPERTIES LABELS tools
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

# The writer with a lowered LBox limit, to reach the switch to XLBox.
add_executable(test_writer test_writer.c)
esajpip_configure_target(test_writer)
target_link_libraries(test_writer PRIVATE jpeg2000_io)
add_test(NAME writer COMMAND test_writer)
set_tests_properties(writer PROPERTIES LABELS tools
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

# The model's checks that need no compiler (spec/check-model.sh --static).
add_test(NAME model_static COMMAND sh ${PROJECT_SOURCE_DIR}/spec/check-model.sh --static)
set_tests_properties(model_static PROPERTIES LABELS tools)
