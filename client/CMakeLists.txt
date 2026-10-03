# The JPIP client: reads JPP-stream responses, keeps the data-bins they
# deliver, writes a codestream back from them and decodes it. Plain C on the
# jpeg2000 library and the vendored OpenJPEG; its tests are in tests/client/.

# OpenJPEG (vendor/openjpeg/README.md): the library's sources, as upstream
# builds them without threads.
set(esajpip_openjpeg_sources
    bio.c cio.c dwt.c event.c function_list.c ht_dec.c image.c invert.c j2k.c
    jp2.c mct.c mqc.c openjpeg.c opj_malloc.c pi.c sparse_array.c t1.c t2.c
    tcd.c tgt.c thread.c)
list(TRANSFORM esajpip_openjpeg_sources PREPEND vendor/openjpeg/)
add_library(esajpip_openjpeg STATIC ${esajpip_openjpeg_sources})
target_compile_options(esajpip_openjpeg PRIVATE -w)
target_include_directories(esajpip_openjpeg SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/openjpeg)
target_link_libraries(esajpip_openjpeg PUBLIC m)

add_library(esajpip_client STATIC
    hv_jpp.c
    hv_cache.c
    hv_frame.c
    hv_reconstruct.c
    hv_metadata.c
    hv_image.c)
esajpip_configure_target(esajpip_client)
target_include_directories(esajpip_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(esajpip_client PUBLIC jpeg2000 PRIVATE esajpip_openjpeg)

# hv_jpp2j2k: the bodies of JPIP responses, as saved by curl, to a .j2k file.
add_executable(hv_jpp2j2k hv_jpp2j2k.c ${PROJECT_SOURCE_DIR}/tools/hv_file.c)
esajpip_configure_target(hv_jpp2j2k)
target_link_libraries(hv_jpp2j2k PRIVATE esajpip_client)

# The WebAssembly client: `cmake --build <dir> --target esajpip_client_wasm`
# writes <dir>/client/web/, to be served as it is (README.md). Its C
# compiler is zig's, which carries the WASI libc that OpenJPEG needs.
find_program(ESAJPIP_ZIG zig DOC "zig, whose C compiler builds the WebAssembly client")
if(NOT ESAJPIP_ZIG)
    message(STATUS "client: zig not found, no esajpip_client_wasm target")
    return()
endif()

set(esajpip_web ${CMAKE_CURRENT_BINARY_DIR}/web)
set(esajpip_wasm_flags -target wasm32-wasi -O2
    -I${PROJECT_SOURCE_DIR} -I${CMAKE_CURRENT_SOURCE_DIR}
    -I${PROJECT_SOURCE_DIR}/jpeg2000 -I${PROJECT_SOURCE_DIR}/jpeg2000/generated
    -isystem ${CMAKE_CURRENT_SOURCE_DIR}/vendor/openjpeg)
# esajpip_wasm_object(<source> <flag>...): its object, added to esajpip_wasm_objects.
function(esajpip_wasm_object source)
    get_filename_component(name ${source} NAME_WE)
    set(object ${CMAKE_CURRENT_BINARY_DIR}/wasm/${name}.o)
    add_custom_command(
        OUTPUT ${object}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/wasm
        COMMAND ${ESAJPIP_ZIG} cc ${esajpip_wasm_flags} ${ARGN}
                -MD -MF ${object}.d -MT ${object} -c ${source} -o ${object}
        DEPENDS ${source}
        DEPFILE ${object}.d
        VERBATIM)
    set(esajpip_wasm_objects ${esajpip_wasm_objects} ${object} PARENT_SCOPE)
endfunction()

# Our sources and what hv_reconstruct takes of the jpeg2000 library, with
# the project's warnings; OpenJPEG without.
foreach(source hv_wasm.c hv_jpp.c hv_cache.c hv_frame.c hv_reconstruct.c hv_metadata.c hv_image.c)
    esajpip_wasm_object(${CMAKE_CURRENT_SOURCE_DIR}/${source}
        -std=c11 -Wall -Wextra -Wno-unused-parameter)
endforeach()
foreach(source hv_reader.c hv_rules.c hv_mapping.c hv_error.c
        generated/asn1crt.c generated/asn1crt_encoding.c generated/asn1crt_encoding_acn.c
        generated/asn1crt_encoding_uper.c generated/j2k-headers.c generated/j2k-codestream.c
        generated/jp2-boxes.c generated/jpeg2000-io.c)
    esajpip_wasm_object(${PROJECT_SOURCE_DIR}/jpeg2000/${source}
        -std=c11 -Wall -Wextra -Wno-unused-parameter)
endforeach()
foreach(source ${esajpip_openjpeg_sources})
    esajpip_wasm_object(${CMAKE_CURRENT_SOURCE_DIR}/${source} -w)
endforeach()

# A module without main: hv_wasm.c exports its entry points.
add_custom_command(
    OUTPUT ${esajpip_web}/esajpip_client.wasm
    COMMAND ${CMAKE_COMMAND} -E make_directory ${esajpip_web}
    COMMAND ${ESAJPIP_ZIG} cc -target wasm32-wasi -mexec-model=reactor -s
            -Wl,-z,stack-size=1048576 ${esajpip_wasm_objects}
            -o ${esajpip_web}/esajpip_client.wasm
    DEPENDS ${esajpip_wasm_objects}
    COMMENT "client: linking esajpip_client.wasm"
    VERBATIM)
# The library (js/) and its demonstration page, side by side with the module.
set(esajpip_web_files)
foreach(file js/jpip_source.mjs js/jpip_worker.mjs js/jpip_channel.mjs demo/index.html)
    get_filename_component(name ${file} NAME)
    add_custom_command(
        OUTPUT ${esajpip_web}/${name}
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/${file} ${esajpip_web}/${name}
        DEPENDS ${file}
        VERBATIM)
    list(APPEND esajpip_web_files ${esajpip_web}/${name})
endforeach()
add_custom_target(esajpip_client_wasm
    DEPENDS ${esajpip_web}/esajpip_client.wasm ${esajpip_web_files})
