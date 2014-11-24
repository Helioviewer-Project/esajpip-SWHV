
configure_file(esajpip/server.cfg ${CMAKE_CURRENT_BINARY_DIR}/server.cfg)

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
	list(APPEND APP_SRCS ${ESAJPIP_TOP}/${SRC})
endforeach()

# -DSILENT_MODE
# -DSHOW_TRACES
# -UNDEBUG
add_definitions(-D_NO_READPROC -D_NO_FAST_FILE -DNO_COLORS)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	add_definitions(-D_PLATFORM_LINUX)
endif()

if(APPLE)
	add_definitions(-std=c++11)
	set(CMAKE_EXE_LINKER_FLAGS "-lc++abi")
endif()

if(UNIX AND NOT APPLE)
	add_definitions(-D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600)
	set(CMAKE_EXE_LINKER_FLAGS "-lrt")
endif()

include_directories(BEFORE . ${ESAJPIP_TOP} ${LIBCONFIG}/lib ${LOG4CPP}/include)
add_executable(esajpip ${ESAJPIP_TOP}/esa_jpip_server.cc ${APP_SRCS} ${ALL_SRCS})

target_link_libraries(esajpip config++ log4cpp pthread)
install(TARGETS esajpip DESTINATION server/esajpip)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/server.cfg DESTINATION server/esajpip)
