
cmake_minimum_required(VERSION 2.8)
set(CMAKE_INSTALL_PREFIX "/swhv/support")

include_directories(.)

set(LOG4CPP log4cpp-1.1)
add_subdirectory(${LOG4CPP})
