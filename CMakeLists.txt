
cmake_minimum_required(VERSION 2.8)
set(CMAKE_INSTALL_PREFIX "/swhv/support")

macro(include_before ...)
	include_directories(BEFORE ${ARGV})
endmacro(include_before)

set(TOP ${CMAKE_SOURCE_DIR})

set(LIBCONFIG ${TOP}/libconfig/libconfig-1.4.9)
set(LOG4CPP   ${TOP}/log4cpp/log4cpp-1.1)

add_subdirectory(libconfig)
add_subdirectory(log4cpp)
add_subdirectory(esajpip)
