# hv_merge: the C port of hvJP2K's JPX merger, on the jpeg2000_io
# library in ../lib.

add_executable(hv_merge hv_merge.c merge.c)
esajpip_configure_target(hv_merge)
target_sources(hv_merge PRIVATE ${PROJECT_SOURCE_DIR}/tools/hv_file.c)
target_include_directories(hv_merge PRIVATE ${PROJECT_SOURCE_DIR}/tools)
target_link_libraries(hv_merge PRIVATE jpeg2000_io)

