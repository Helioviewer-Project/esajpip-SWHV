
cmake_minimum_required(VERSION 2.8)
SET(CMAKE_INSTALL_PREFIX "/swhv/esajpip")

SET(CMAKE_BUILD_TYPE RelWithDebInfo)

SET(TOP ${CMAKE_SOURCE_DIR})

SET(LIBCONFIG ${TOP}/libconfig/libconfig-1.4.9)
SET(LOG4CPP   ${TOP}/log4cpp/log4cpp-1.1)

ADD_SUBDIRECTORY(libconfig)
ADD_SUBDIRECTORY(log4cpp)
ADD_SUBDIRECTORY(esajpip)
