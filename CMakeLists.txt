cmake_minimum_required(VERSION 3.16)

project(esajpip LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

include(CTest)
include(GNUInstallDirs)
find_package(Threads REQUIRED)

set(SIDC_INSTALL_LIB ${CMAKE_INSTALL_LIBDIR})

set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/${SIDC_INSTALL_LIB})

macro(sidc_install_lib target)
    set_target_properties(${target} PROPERTIES
        VERSION 1.0
        SOVERSION 1.0
        INSTALL_NAME_DIR ${CMAKE_INSTALL_PREFIX}/${SIDC_INSTALL_LIB})
    install(TARGETS ${target} DESTINATION ${SIDC_INSTALL_LIB})
endmacro()

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

set(LOG4CPP   ${CMAKE_CURRENT_SOURCE_DIR}/log4cpp/log4cpp-1.1.6)

add_subdirectory(log4cpp)
add_subdirectory(esajpip)
