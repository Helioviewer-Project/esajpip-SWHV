cmake_minimum_required(VERSION 3.16)

project(esajpip LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

include(CTest)
find_package(Threads REQUIRED)

function(esajpip_enable_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        "$<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>")
endfunction()

#-fsanitize=address,undefined
#-fsanitize=memory -fsanitize-memory-track-origins"
#-fsanitize=thread

find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)
pkg_check_modules(PKG REQUIRED IMPORTED_TARGET glib-2.0>=2.36)

configure_file(server.ini ${CMAKE_CURRENT_BINARY_DIR}/server.ini)

set(ESAJPIP_TOP src)
set(ESAJPIP_SRCS
    ${ESAJPIP_TOP}/app_config.cc
    ${ESAJPIP_TOP}/app_info.cc
    ${ESAJPIP_TOP}/data/file_segment.cc
    ${ESAJPIP_TOP}/http/header.cc
    ${ESAJPIP_TOP}/jpeg2000/coding_parameters.cc
    ${ESAJPIP_TOP}/jpeg2000/file_manager.cc
    ${ESAJPIP_TOP}/jpeg2000/image_index.cc
    ${ESAJPIP_TOP}/jpip/databin_server.cc
    ${ESAJPIP_TOP}/jpip/databin_writer.cc
    ${ESAJPIP_TOP}/jpip/request.cc
    ${ESAJPIP_TOP}/net/socket.cc
    ${ESAJPIP_TOP}/server/channel.cc
    ${ESAJPIP_TOP}/server/child.cc
    ${ESAJPIP_TOP}/server/connection_queue.cc
    ${ESAJPIP_TOP}/server/initial_request.cc
    ${ESAJPIP_TOP}/trace.cc)

add_executable(esajpip ${ESAJPIP_TOP}/esa_jpip_server.cc ${ESAJPIP_SRCS})
esajpip_enable_warnings(esajpip)
set_target_properties(esajpip PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES)
target_compile_options(esajpip PRIVATE -UNDEBUG)
target_include_directories(esajpip BEFORE PRIVATE
    .
    ${ESAJPIP_TOP})
target_link_libraries(esajpip PRIVATE
    PkgConfig::PKG
    ZLIB::ZLIB
    Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(esajpip PRIVATE
        _PLATFORM_LINUX
        _POSIX_C_SOURCE=200112L
        _XOPEN_SOURCE=600)
    target_link_libraries(esajpip PRIVATE rt)
elseif(APPLE)
    target_link_libraries(esajpip PRIVATE c++abi)
endif()

install(TARGETS esajpip DESTINATION server/esajpip)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/server.ini DESTINATION server/esajpip)

if(BUILD_TESTING)
    add_executable(logging_test tests/logging_test.cc ${ESAJPIP_TOP}/trace.cc)
    esajpip_enable_warnings(logging_test)
    set_target_properties(logging_test PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)
    target_compile_definitions(logging_test PRIVATE ESAJPIP_LOG_LIMIT=1024)
    target_include_directories(logging_test PRIVATE ${ESAJPIP_TOP})
    add_test(NAME logging COMMAND logging_test)

    add_executable(protocol_test
        tests/protocol_test.cc
        ${ESAJPIP_TOP}/app_config.cc
        ${ESAJPIP_TOP}/data/file_segment.cc
        ${ESAJPIP_TOP}/http/header.cc
        ${ESAJPIP_TOP}/jpeg2000/coding_parameters.cc
        ${ESAJPIP_TOP}/jpip/databin_writer.cc
        ${ESAJPIP_TOP}/jpip/request.cc
        ${ESAJPIP_TOP}/server/connection_queue.cc
        ${ESAJPIP_TOP}/server/initial_request.cc
        ${ESAJPIP_TOP}/trace.cc)
    esajpip_enable_warnings(protocol_test)
    set_target_properties(protocol_test PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)
    target_include_directories(protocol_test PRIVATE
        ${ESAJPIP_TOP})
    target_link_libraries(protocol_test PRIVATE PkgConfig::PKG Threads::Threads)
    add_test(NAME protocol COMMAND protocol_test)

    add_executable(jpeg2000_test
        tests/jpeg2000_test.cc
        ${ESAJPIP_TOP}/data/file_segment.cc
        ${ESAJPIP_TOP}/jpeg2000/coding_parameters.cc
        ${ESAJPIP_TOP}/jpeg2000/file_manager.cc
        ${ESAJPIP_TOP}/jpeg2000/image_index.cc
        ${ESAJPIP_TOP}/jpip/databin_server.cc
        ${ESAJPIP_TOP}/jpip/databin_writer.cc
        ${ESAJPIP_TOP}/jpip/request.cc
        ${ESAJPIP_TOP}/trace.cc)
    esajpip_enable_warnings(jpeg2000_test)
    set_target_properties(jpeg2000_test PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)
    target_include_directories(jpeg2000_test PRIVATE
        ${ESAJPIP_TOP})
    target_link_libraries(jpeg2000_test PRIVATE PkgConfig::PKG Threads::Threads)
    add_test(NAME jpeg2000 COMMAND jpeg2000_test)
endif()
