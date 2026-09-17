cmake_minimum_required(VERSION 3.16)

project(esajpip LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

include(CTest)
find_package(Threads REQUIRED)

option(ESAJPIP_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(ESAJPIP_SANITIZE AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "ESAJPIP_SANITIZE requires Clang or GCC")
endif()

function(esajpip_configure_target target)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        "$<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>")
    if(ESAJPIP_SANITIZE)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined)
    endif()
endfunction()

find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)
pkg_check_modules(PKG REQUIRED IMPORTED_TARGET glib-2.0>=2.36)

configure_file(server.ini ${CMAKE_CURRENT_BINARY_DIR}/server.ini)

set(ESAJPIP_TOP src)
set(ESAJPIP_SRCS
    ${ESAJPIP_TOP}/config.cc
    ${ESAJPIP_TOP}/data/file_segment.cc
    ${ESAJPIP_TOP}/http/header.cc
    ${ESAJPIP_TOP}/jpeg2000/coding_parameters.cc
    ${ESAJPIP_TOP}/jpeg2000/file_manager.cc
    ${ESAJPIP_TOP}/jpeg2000/image_index.cc
    ${ESAJPIP_TOP}/jpip/databin_server.cc
    ${ESAJPIP_TOP}/jpip/databin_writer.cc
    ${ESAJPIP_TOP}/jpip/query.cc
    ${ESAJPIP_TOP}/jpip/request.cc
    ${ESAJPIP_TOP}/server/channel.cc
    ${ESAJPIP_TOP}/server/connection_queue.cc
    ${ESAJPIP_TOP}/server/initial_request.cc
    ${ESAJPIP_TOP}/server/server.cc
    ${ESAJPIP_TOP}/server/supervisor.cc
    ${ESAJPIP_TOP}/trace.cc)

add_library(esajpip_core STATIC ${ESAJPIP_SRCS})
esajpip_configure_target(esajpip_core)
target_compile_options(esajpip_core PRIVATE -UNDEBUG)
target_include_directories(esajpip_core BEFORE PUBLIC
    ${ESAJPIP_TOP})
target_link_libraries(esajpip_core PUBLIC
    PkgConfig::PKG
    ZLIB::ZLIB
    Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(esajpip_core PRIVATE
        _PLATFORM_LINUX
        _POSIX_C_SOURCE=200112L
        _XOPEN_SOURCE=600)
elseif(APPLE)
    target_link_libraries(esajpip_core PUBLIC c++abi)
endif()

add_executable(esajpip ${ESAJPIP_TOP}/main.cc)
esajpip_configure_target(esajpip)
target_compile_options(esajpip PRIVATE -UNDEBUG)
target_link_libraries(esajpip PRIVATE esajpip_core)

install(TARGETS esajpip RUNTIME DESTINATION bin)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/server.ini DESTINATION bin)

if(BUILD_TESTING)
    add_executable(logging_test tests/logging_test.cc ${ESAJPIP_TOP}/trace.cc)
    esajpip_configure_target(logging_test)
    target_compile_definitions(logging_test PRIVATE ESAJPIP_LOG_LIMIT=1024)
    target_include_directories(logging_test PRIVATE ${ESAJPIP_TOP})
    target_link_libraries(logging_test PRIVATE Threads::Threads)
    add_test(NAME logging COMMAND logging_test)

    add_executable(protocol_test tests/protocol_test.cc)
    esajpip_configure_target(protocol_test)
    target_link_libraries(protocol_test PRIVATE esajpip_core)
    add_test(NAME protocol COMMAND protocol_test)

    add_executable(jpeg2000_test tests/jpeg2000_test.cc)
    esajpip_configure_target(jpeg2000_test)
    target_link_libraries(jpeg2000_test PRIVATE esajpip_core)
    add_test(NAME jpeg2000 COMMAND jpeg2000_test)
endif()
