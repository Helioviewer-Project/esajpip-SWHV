
include(CheckSymbolExists)

set(LIBCONFIG_SRCS
    ${LIBCONFIG}/lib/grammar.c
    ${LIBCONFIG}/lib/libconfig.c
    ${LIBCONFIG}/lib/libconfigcpp.cc
    ${LIBCONFIG}/lib/scanctx.c
    ${LIBCONFIG}/lib/scanner.c
    ${LIBCONFIG}/lib/strbuf.c
    ${LIBCONFIG}/lib/strvec.c
    ${LIBCONFIG}/lib/util.c
    ${LIBCONFIG}/lib/wincompat.c)

add_library(config SHARED ${LIBCONFIG_SRCS})
set_target_properties(config PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES)
target_include_directories(config PRIVATE ${LIBCONFIG}/lib)

if(APPLE)
    set(LIBCONFIG_LOCALE_HEADER xlocale.h)
else()
    set(LIBCONFIG_LOCALE_HEADER locale.h)
endif()

foreach(SYMBOL uselocale newlocale freelocale)
    string(TOUPPER ${SYMBOL} SYMBOL_UPPER)
    check_symbol_exists(${SYMBOL} ${LIBCONFIG_LOCALE_HEADER} HAVE_${SYMBOL_UPPER})
    if(HAVE_${SYMBOL_UPPER})
        target_compile_definitions(config PRIVATE HAVE_${SYMBOL_UPPER})
    endif()
endforeach()

sidc_install_lib(config)
