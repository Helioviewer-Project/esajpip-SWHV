
cmake_minimum_required(VERSION 2.8)
SET(CMAKE_INSTALL_PREFIX "/swhv/esajpip")

SET(CMAKE_BUILD_TYPE RelWithDebInfo)

SET(TOP ${CMAKE_SOURCE_DIR})

SET(LIBCONFIG ${TOP}/libconfig/libconfig-1.4.9)
SET(LOG4CPP   ${TOP}/log4cpp/log4cpp-1.1)

SET(ESAJPIP_LISTEN_PORT 8090)
SET(ESAJPIP_DIR_IMAGE   "/data/jp2")
SET(ESAJPIP_DIR_CACHE   "/srv/www/htdocs/helioviewer/cache")
SET(ESAJPIP_DIR_LOG     "/srv/www/htdocs/helioviewer/log")

ADD_SUBDIRECTORY(libconfig)
ADD_SUBDIRECTORY(log4cpp)
ADD_SUBDIRECTORY(esajpip)
