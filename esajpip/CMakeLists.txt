
project(esajpip)

set(ESAJPIP_TOP esajpip/src)
aux_source_directory(${ESAJPIP_TOP}/data     DATA_SRCS)
aux_source_directory(${ESAJPIP_TOP}/http     HTTP_SRCS)
aux_source_directory(${ESAJPIP_TOP}/ipc      IPC_SRCS)
aux_source_directory(${ESAJPIP_TOP}/jpeg2000 JPEG2000_SRCS)
aux_source_directory(${ESAJPIP_TOP}/jpip     JPIP_SRCS)
aux_source_directory(${ESAJPIP_TOP}/net      NET_SRCS)
set(ALL_SRCS ${DATA_SRCS} ${HTTP_SRCS} ${IPC_SRCS} ${JPEG2000_SRCS} ${JPIP_SRCS} ${NET_SRCS})

set(CORE_SRCS
	base.cc
    trace.cc
	app_info.cc
	app_config.cc
	args_parser.cc
	client_info.cc
	client_manager.cc)

foreach(SRC ${CORE_SRCS})
    set(APP_SRCS ${APP_SRCS} ${ESAJPIP_TOP}/${SRC})
endforeach(SRC)

add_definitions(-D_NO_READPROC -D_NO_FAST_FILE)
include_before(. ${ESAJPIP_TOP} ${LIBCONFIG}/lib ${LOG4CPP}/include)
add_executable(esajpip ${ESAJPIP_TOP}/esa_jpip_server.cc ${APP_SRCS} ${ALL_SRCS})
target_link_libraries(esajpip config++ log4cpp)
install(TARGETS esajpip DESTINATION ".")
