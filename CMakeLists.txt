cmake_minimum_required(VERSION 3.5)

project(esajpip)

set(CMAKE_BUILD_TYPE RelWithDebInfo)

set(SIDC_PROJECT_NAME   ${PROJECT_NAME})
set(SIDC_INSTALL_LIB    ${CMAKE_INSTALL_PREFIX}/lib)

set(CMAKE_INSTALL_RPATH ${SIDC_INSTALL_LIB})

macro(sidc_install_lib target)
    set_target_properties(${target} PROPERTIES VERSION 1.0 SOVERSION 1.0 INSTALL_NAME_DIR ${SIDC_INSTALL_LIB})
    install(TARGETS ${target} EXPORT ${SIDC_PROJECT_NAME} DESTINATION ${SIDC_INSTALL_LIB})
    install(EXPORT ${SIDC_PROJECT_NAME} DESTINATION ${SIDC_INSTALL_LIB})
endmacro()

#set(CMAKE_CXX_COMPILER $ENV{LOCAL}/clang+llvm-3.5.0-macosx-apple-darwin/bin/clang)
#set(CMAKE_C_COMPILER   $ENV{LOCAL}/clang+llvm-3.5.0-macosx-apple-darwin/bin/clang)
set(EXTRA_FLAGS     "-g -fno-omit-frame-pointer -W -Wextra -Wno-unused-parameter")
#-fsanitize=address,undefined
#-fsanitize=memory -fsanitize-memory-track-origins"
#-fsanitize=thread

set(CMAKE_C_FLAGS_RELWITHDEBINFO   "${CMAKE_C_FLAGS_RELWITHDEBINFO}   ${EXTRA_FLAGS}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${EXTRA_FLAGS}")

set(LIBCONFIG ${CMAKE_CURRENT_SOURCE_DIR}/libconfig/libconfig-1.7.3)
set(LOG4CPP   ${CMAKE_CURRENT_SOURCE_DIR}/log4cpp/log4cpp-1.1.4)

add_definitions(-pthread)

add_subdirectory(libconfig)
add_subdirectory(log4cpp)
add_subdirectory(esajpip)
