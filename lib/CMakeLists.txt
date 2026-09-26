# jpeg2000_io: the JPEG 2000 reader/writer, for hv_transcode, hv_merge and later the
# server. hv_reader, hv_rules, hv_writer and hv_geometry are hand-written; generated/ is produced
# from the model in ../spec by generate.sh (asn1scc is not part of the
# build).

# The generated modules are those of ../spec/modules, the one list that
# generate.sh and ../spec/check-model.sh read too.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../spec/modules jpeg2000_modules
    REGEX "^[a-z0-9-]+$")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../spec/modules)
set(jpeg2000_generated
    generated/asn1crt.c
    generated/asn1crt_encoding.c
    generated/asn1crt_encoding_acn.c
    generated/asn1crt_encoding_uper.c)
foreach(module ${jpeg2000_modules})
    list(APPEND jpeg2000_generated generated/${module}.c)
endforeach()

add_library(jpeg2000_io STATIC
    hv_reader.c
    hv_rules.c
    hv_writer.c
    hv_geometry.c
    hv_mapping.c
    hv_error.c
    hv_file.c
    hv_served.c
    ${jpeg2000_generated})
esajpip_configure_target(jpeg2000_io)
target_include_directories(jpeg2000_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/generated)

# hv_walk: checks JPEG 2000 files with the reader, and with -w rewrites them
# with the writer.
add_executable(hv_walk hv_walk.c)
esajpip_configure_target(hv_walk)
target_link_libraries(hv_walk PRIVATE jpeg2000_io)

# Tests (test/), with hv_transcode's.
if(ESAJPIP_TOOL_TESTS)
    add_subdirectory(test)
endif()
