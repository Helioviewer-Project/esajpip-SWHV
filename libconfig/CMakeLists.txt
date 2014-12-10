
aux_source_directory(${LIBCONFIG}/lib LIBCONFIG_SRCS)

include_directories(.)
add_definitions(-DHAVE_CONFIG_H=1)

add_library(config SHARED ${LIBCONFIG_SRCS})
sidc_install_lib(config)
