# hv_merge: the C port of hvJP2K's JPX merger, on the jpeg2000_io
# library in ../lib.

add_executable(hv_merge hv_merge.c merge.c)
esajpip_configure_target(hv_merge)
target_link_libraries(hv_merge PRIVATE jpeg2000_io)

# Tests (test/), separate from the server's.
if(ESAJPIP_TOOL_TESTS)
    add_subdirectory(test)
endif()
