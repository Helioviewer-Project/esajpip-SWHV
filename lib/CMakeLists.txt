# jpeg2000_io: the JPEG 2000 reader/writer, for hv_transcode and later the
# server. hv_reader is hand-written; generated/ is produced from the model in
# ../spec by generate.sh (asn1scc is not part of the build).

add_library(jpeg2000_io STATIC
    hv_reader.c
    hv_mapping.c
    generated/asn1crt.c
    generated/asn1crt_encoding.c
    generated/asn1crt_encoding_acn.c
    generated/asn1crt_encoding_uper.c
    generated/j2k-codestream.c
    generated/j2k-headers.c
    generated/jp2-boxes.c
    generated/jpeg2000-io.c)
esajpip_configure_target(jpeg2000_io)
target_include_directories(jpeg2000_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/generated)

# hv_walk: checks the framing of JPEG 2000 files with the reader.
add_executable(hv_walk hv_walk.c)
esajpip_configure_target(hv_walk)
target_link_libraries(hv_walk PRIVATE jpeg2000_io)
