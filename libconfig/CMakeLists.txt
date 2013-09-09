
cmake_minimum_required(VERSION 2.8)
set(CMAKE_INSTALL_PREFIX "/swhv/support")

project(libconfig)

set(LIBCONFIG libconfig-1.4.9)
aux_source_directory(${LIBCONFIG}/lib LIBCONFIG_SRCS)

include_directories(.)
add_definitions(-DHAVE_CONFIG_H=1)
add_library(libconfig++ STATIC ${LIBCONFIG_SRCS})

install(TARGETS libconfig++ DESTINATION lib)
install(FILES ${LIBCONFIG}/lib/libconfig.h++ DESTINATION include)
