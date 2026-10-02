cmake_minimum_required(VERSION 3.20)

project(esajpip LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

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

# Libraries, format tools and server runtime.
add_subdirectory(jpeg2000)
add_subdirectory(jpip)
add_subdirectory(transcode)
add_subdirectory(merge)
add_subdirectory(server)
add_subdirectory(client)

# Explicit benchmark target; excluded from normal builds and installation.
add_executable(jpip_bench EXCLUDE_FROM_ALL tools/jpip_bench.cc)
esajpip_configure_target(jpip_bench)
target_link_libraries(jpip_bench PRIVATE esajpip_server)

install(TARGETS esajpip RUNTIME DESTINATION bin)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/server.ini DESTINATION bin)

# Tests: tests/ owns the gate, the options, the helpers and every target,
# including the fuzz ones. enable_testing() is here because CTest reads the
# top-level CTestTestfile.cmake.
enable_testing()
add_subdirectory(tests)
