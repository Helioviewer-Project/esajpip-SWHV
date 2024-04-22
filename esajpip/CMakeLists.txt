
if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

include(FindPkgConfig)
pkg_check_modules(PKG REQUIRED glib-2.0>=2.36 libgsf-1)

configure_file(esajpip/server.cfg ${CMAKE_CURRENT_BINARY_DIR}/server.cfg)

set(ESAJPIP_TOP esajpip/src)
aux_source_directory(${ESAJPIP_TOP}/data     DATA_SRCS)
aux_source_directory(${ESAJPIP_TOP}/http     HTTP_SRCS)
aux_source_directory(${ESAJPIP_TOP}/jpeg2000 JPEG2000_SRCS)
aux_source_directory(${ESAJPIP_TOP}/jpip     JPIP_SRCS)
aux_source_directory(${ESAJPIP_TOP}/net      NET_SRCS)

set(CORE_SRCS
    app_info.cc
    app_config.cc
    args_parser.cc
    client_manager.cc
    mongoose/mongoose.c
    z/zfilter.c)

foreach(SRC ${CORE_SRCS})
    list(APPEND APP_SRCS ${ESAJPIP_TOP}/${SRC})
endforeach()

# -DSILENT_MODE
# -DSHOW_TRACES
add_definitions(-D_NO_READPROC -DNO_COLORS -UNDEBUG -DLOG4CPP_HAVE_SSTREAM)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_definitions(-D_PLATFORM_LINUX)
endif()

if(APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "-lc++abi")
endif()

if(UNIX AND NOT APPLE)
    add_definitions(-D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600)
    set(CMAKE_EXE_LINKER_FLAGS "-lrt")
endif()

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -std=c++11")

include_directories(BEFORE . ${ESAJPIP_TOP} ${LIBCONFIG}/lib ${LOG4CPP}/include ${PKG_INCLUDE_DIRS})
link_directories(${PKG_LIBRARY_DIRS})

add_executable(esajpip ${ESAJPIP_TOP}/esa_jpip_server.cc ${APP_SRCS} ${HTTP_SRCS} ${JPIP_SRCS} ${NET_SRCS} ${DATA_SRCS} ${JPEG2000_SRCS} ${ESAJPIP_TOP}/trace.cc)
target_link_libraries(esajpip ${PKG_LIBRARIES} config log4cpp pthread)

#add_executable(packet_information ${ESAJPIP_TOP}/packet_information.cc ${APP_SRCS} ${HTTP_SRCS} ${JPIP_SRCS} ${NET_SRCS} ${DATA_SRCS} ${JPEG2000_SRCS} ${ESAJPIP_TOP}/trace.cc)
#target_link_libraries(packet_information ${PKG_LIBRARIES} config log4cpp pthread)

install(TARGETS esajpip DESTINATION server/esajpip)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/server.cfg DESTINATION server/esajpip)
