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

add_subdirectory(esajpip)
