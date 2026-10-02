# Tests of the JPIP client (client/). All but test_cache link jpip, so that
# the client is checked against what the server's own code writes.
#
#   client_cache         the store's append and completeness rules
#   client_jpp           bytes from jpip::DataBinWriter read back by the client
#   client_jpp_malformed damaged versions of those bytes, all rejected
#   client_reconstruct   the corpus, the transcoder's reference images and
#                        the merger's reference movie served by
#                        jpip::DataBinServer, stored, written back as
#                        codestreams and, the images, decoded; the movie's
#                        frame count, XML and color tables

add_executable(test_client_cache test_cache.c)
esajpip_configure_target(test_client_cache)
target_link_libraries(test_client_cache PRIVATE esajpip_client)
esajpip_add_test(client_cache COMMAND test_client_cache LABELS client)

add_executable(test_client_image test_image.c)
esajpip_configure_target(test_client_image)
target_link_libraries(test_client_image PRIVATE esajpip_client esajpip_openjpeg)
esajpip_add_test(client_image COMMAND test_client_image LABELS client)

foreach(name jpp_roundtrip jpp_malformed reconstruct)
    add_executable(test_client_${name} test_${name}.cc)
    esajpip_configure_target(test_client_${name})
    target_link_libraries(test_client_${name} PRIVATE esajpip_client jpip)
endforeach()
esajpip_add_test(client_jpp COMMAND test_client_jpp_roundtrip LABELS client)
esajpip_add_test(client_jpp_malformed COMMAND test_client_jpp_malformed LABELS client)

target_compile_definitions(test_client_reconstruct PRIVATE
    VECTORS="${PROJECT_SOURCE_DIR}/tests/vectors/j2k"
    IMAGES="${PROJECT_SOURCE_DIR}/tests/transcode/fixtures/kakadu"
    MOVIE="${PROJECT_SOURCE_DIR}/tests/merge/fixtures/expected/merged.jpx")
esajpip_add_test(client_reconstruct COMMAND test_client_reconstruct LABELS client TIMEOUT 60)
