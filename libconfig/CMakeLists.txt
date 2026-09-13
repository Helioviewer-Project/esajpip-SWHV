
aux_source_directory(${LIBCONFIG}/lib LIBCONFIG_SRCS)

add_library(config SHARED ${LIBCONFIG_SRCS})
target_compile_definitions(config PRIVATE HAVE_CONFIG_H=1)
target_include_directories(config PRIVATE .)
sidc_install_lib(config)
