
cmake_minimum_required(VERSION 2.8)
set(CMAKE_INSTALL_PREFIX "/swhv/support")

set(LIBCONFIG libconfig/libconfig-1.4.9)
set(LOG4CPP   log4cpp/log4cpp-1.1)

add_subdirectory(libconfig)
add_subdirectory(log4cpp)
