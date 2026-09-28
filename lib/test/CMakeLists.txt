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

# The writer against the corpus: every vector the reader accepts, written
# again from what it decoded (hv_rewrite).
add_executable(test_rewrite test_rewrite.c)
esajpip_configure_target(test_rewrite)
target_link_libraries(test_rewrite PRIVATE jpeg2000_io)
add_test(NAME writer_corpus
    COMMAND test_rewrite ${PROJECT_SOURCE_DIR}/tests/vectors/j2k)
set_tests_properties(writer_corpus PROPERTIES LABELS tools
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

# hv_file: replacing the tools' output files.
add_executable(test_file test_file.c)
esajpip_configure_target(test_file)
target_link_libraries(test_file PRIVATE jpeg2000_io)
add_test(NAME output_file COMMAND test_file)
set_tests_properties(output_file PROPERTIES LABELS tools
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

# The writer with a lowered LBox limit, to reach the switch to XLBox.
add_executable(test_writer test_writer.c)
esajpip_configure_target(test_writer)
target_link_libraries(test_writer PRIVATE jpeg2000_io)
add_test(NAME writer COMMAND test_writer)
set_tests_properties(writer PROPERTIES LABELS tools
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

# hv_served.c included, with open, fstat, read and malloc that misbehave on
# demand.
add_executable(test_served test_served.c)
esajpip_configure_target(test_served)
target_link_libraries(test_served PRIVATE jpeg2000_io)
add_test(NAME served COMMAND test_served ${PROJECT_SOURCE_DIR}/tests/vectors/j2k)
set_tests_properties(served PROPERTIES LABELS tools
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

# The reader's framing, offsets and errors, byte by byte (hv_reader.c
# included, with allocations that fail on demand).
add_executable(test_reader test_reader.c)
esajpip_configure_target(test_reader)
target_link_libraries(test_reader PRIVATE jpeg2000_io)
add_test(NAME reader COMMAND test_reader ${PROJECT_SOURCE_DIR}/tests/vectors/j2k)
set_tests_properties(reader PROPERTIES LABELS tools
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

# hv_geometry against T.800 computed a second way (hv_geometry.c included,
# with allocations that fail on demand).
add_executable(test_geometry test_geometry.c)
esajpip_configure_target(test_geometry)
target_link_libraries(test_geometry PRIVATE jpeg2000_io)
add_test(NAME geometry COMMAND test_geometry)
set_tests_properties(geometry PROPERTIES LABELS tools
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

# The model's checks that need no compiler (spec/check-model.sh --static).
add_test(NAME model_static COMMAND sh ${PROJECT_SOURCE_DIR}/spec/check-model.sh --static)
set_tests_properties(model_static PROPERTIES LABELS tools)
