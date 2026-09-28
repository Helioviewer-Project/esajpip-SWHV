# The server's tests. They link the server's own core library, not
# jpeg2000_io: they are the second, independent JPEG 2000 implementation.
# jpeg2000_test reads the same corpus as the reader's tests
# (tests/vectors/j2k) and checks the manifest labels against the server's
# parser.

add_executable(logging_test logging_test.cc ${PROJECT_SOURCE_DIR}/${ESAJPIP_TOP}/trace.cc)
esajpip_configure_target(logging_test)
target_compile_definitions(logging_test PRIVATE
    ESAJPIP_LOG_LIMIT=1024
    ESAJPIP_LOG_QUEUE_LIMIT=4096)
target_include_directories(logging_test PRIVATE ${PROJECT_SOURCE_DIR}/${ESAJPIP_TOP})
target_link_libraries(logging_test PRIVATE Threads::Threads)
esajpip_add_test(logging COMMAND logging_test LABELS server)

add_executable(protocol_test protocol_test.cc)
esajpip_configure_target(protocol_test)
target_link_libraries(protocol_test PRIVATE esajpip_core)
esajpip_add_test(protocol COMMAND protocol_test LABELS server)

add_executable(server_test server_test.cc)
esajpip_configure_target(server_test)
target_link_libraries(server_test PRIVATE esajpip_core)
esajpip_add_test(server COMMAND server_test LABELS server)

add_executable(server_connection_test server_connection_test.cc)
esajpip_configure_target(server_connection_test)
target_link_libraries(server_connection_test PRIVATE esajpip_core)
esajpip_add_test(server_connection COMMAND server_connection_test LABELS server)

set_tests_properties(server server_connection PROPERTIES
    RESOURCE_LOCK server_ports)

add_executable(jpeg2000_test jpeg2000_test.cc)
esajpip_configure_target(jpeg2000_test)
target_compile_definitions(jpeg2000_test PRIVATE
    JPEG2000_VECTOR_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/vectors/j2k")
target_link_libraries(jpeg2000_test PRIVATE esajpip_core)
esajpip_add_test(jpeg2000 COMMAND jpeg2000_test LABELS server)
