
project(libconfig)

set(LIBCONFIG_TOP ${CMAKE_SOURCE_DIR}/${LIBCONFIG}/lib)
aux_source_directory(${LIBCONFIG_TOP} LIBCONFIG_SRCS)

include_directories(.)
add_definitions(-DHAVE_CONFIG_H=1)
add_library(libconfig++ STATIC ${LIBCONFIG_SRCS})

install(TARGETS libconfig++ DESTINATION lib)
install(FILES ${LIBCONFIG_TOP}/libconfig.h++ DESTINATION include)
