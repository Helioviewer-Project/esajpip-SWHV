cmake_minimum_required(VERSION 3.16)

project(esajpip LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

find_package(Threads REQUIRED)

option(ESAJPIP_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(ESAJPIP_SANITIZE AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "ESAJPIP_SANITIZE requires Clang or GCC")
endif()

# Use LTO for optimized builds when supported. Explicit CMake IPO settings
# take precedence; keep the default Debug and sanitizer builds without LTO.
if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION AND NOT ESAJPIP_SANITIZE)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ESAJPIP_IPO_SUPPORTED LANGUAGES C CXX)
    if(ESAJPIP_IPO_SUPPORTED)
        foreach(config RELEASE RELWITHDEBINFO MINSIZEREL)
            if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION_${config})
                set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_${config} ON)
            endif()
        endforeach()
    else()
        message(STATUS "LTO is not supported by this toolchain; building without it")
    endif()
endif()

function(esajpip_configure_target target)
    set_target_properties(${target} PROPERTIES
        C_EXTENSIONS OFF
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        # An undeclared function is an int in C89: a pointer it returns
        # (realpath without _XOPEN_SOURCE) would be truncated.
        "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>"
        "$<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>")
    if(ESAJPIP_SANITIZE)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -UNDEBUG)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined)
    endif()
endfunction()

find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)
pkg_check_modules(PKG REQUIRED IMPORTED_TARGET glib-2.0>=2.36)
pkg_check_modules(LLHTTP REQUIRED IMPORTED_TARGET libllhttp)
pkg_check_modules(LIBUV REQUIRED IMPORTED_TARGET libuv>=1.50)

set(ESAJPIP_IMAGE_DIRECTORY "images" CACHE STRING
    "Image directory written to server.ini")
set(ESAJPIP_LOG_DIRECTORY "" CACHE STRING
    "Log directory written to server.ini; empty uses standard output")
if(ESAJPIP_LOG_DIRECTORY)
    set(ESAJPIP_FILE_LOGGING true)
else()
    set(ESAJPIP_FILE_LOGGING false)
endif()
configure_file(server.ini ${CMAKE_CURRENT_BINARY_DIR}/server.ini)

# The JPEG 2000 reader/writer (lib/), built as a static library.
add_subdirectory(lib)
# hv_transcode (transcode/) and hv_merge (merge/), on that library.
add_subdirectory(transcode)
add_subdirectory(merge)

set(ESAJPIP_TOP src)
set(ESAJPIP_SRCS
    ${ESAJPIP_TOP}/config.cc
    ${ESAJPIP_TOP}/data/file_segment.cc
    ${ESAJPIP_TOP}/jpeg2000/coding_parameters.cc
    ${ESAJPIP_TOP}/jpeg2000/file_manager.cc
    ${ESAJPIP_TOP}/jpeg2000/image_index.cc
    ${ESAJPIP_TOP}/jpip/databin_server.cc
    ${ESAJPIP_TOP}/jpip/databin_writer.cc
    ${ESAJPIP_TOP}/jpip/query.cc
    ${ESAJPIP_TOP}/jpip/request.cc
    ${ESAJPIP_TOP}/server/channel_engine.cc
    ${ESAJPIP_TOP}/server/channel_work.cc
    ${ESAJPIP_TOP}/server/connection.cc
    ${ESAJPIP_TOP}/server/request_head.cc
    ${ESAJPIP_TOP}/server/server.cc
    ${ESAJPIP_TOP}/trace.cc)

add_library(esajpip_core STATIC ${ESAJPIP_SRCS})
esajpip_configure_target(esajpip_core)
target_include_directories(esajpip_core BEFORE PUBLIC
    ${ESAJPIP_TOP})
target_link_libraries(esajpip_core PUBLIC
    PkgConfig::PKG
    PkgConfig::LLHTTP
    PkgConfig::LIBUV
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
target_link_libraries(esajpip PRIVATE esajpip_core)

install(TARGETS esajpip RUNTIME DESTINATION bin)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/server.ini DESTINATION bin)

# Tests: tests/ owns the gate, the options, the helpers and every target,
# including the fuzz ones. enable_testing() is here because CTest reads the
# top-level CTestTestfile.cmake.
enable_testing()
add_subdirectory(tests)
