
find_package(PkgConfig REQUIRED)
pkg_check_modules(PKG REQUIRED IMPORTED_TARGET glib-2.0>=2.36 libgsf-1)

configure_file(esajpip/server.cfg ${CMAKE_CURRENT_BINARY_DIR}/server.cfg)

set(ESAJPIP_TOP esajpip/src)
set(ESAJPIP_SRCS
    ${ESAJPIP_TOP}/app_config.cc
    ${ESAJPIP_TOP}/app_info.cc
    ${ESAJPIP_TOP}/args_parser.cc
    ${ESAJPIP_TOP}/client_manager.cc
    ${ESAJPIP_TOP}/data/file_segment.cc
    ${ESAJPIP_TOP}/http/header.cc
    ${ESAJPIP_TOP}/http/protocol.cc
    ${ESAJPIP_TOP}/http/request.cc
    ${ESAJPIP_TOP}/http/response.cc
    ${ESAJPIP_TOP}/jpeg2000/coding_parameters.cc
    ${ESAJPIP_TOP}/jpeg2000/file_manager.cc
    ${ESAJPIP_TOP}/jpeg2000/image_index.cc
    ${ESAJPIP_TOP}/jpip/databin_server.cc
    ${ESAJPIP_TOP}/jpip/databin_writer.cc
    ${ESAJPIP_TOP}/jpip/jpip.cc
    ${ESAJPIP_TOP}/jpip/request.cc
    ${ESAJPIP_TOP}/net/socket.cc
    ${ESAJPIP_TOP}/trace.cc
    ${ESAJPIP_TOP}/z/zfilter.c)

add_executable(esajpip ${ESAJPIP_TOP}/esa_jpip_server.cc ${ESAJPIP_SRCS})
set_target_properties(esajpip PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES)
target_compile_definitions(esajpip PRIVATE _NO_READPROC NO_COLORS LOG4CPP_HAVE_SSTREAM)
target_compile_options(esajpip PRIVATE -UNDEBUG)
target_include_directories(esajpip BEFORE PRIVATE
    .
    ${ESAJPIP_TOP}
    ${LIBCONFIG}/lib
    ${CMAKE_BINARY_DIR}/log4cpp/include
    ${LOG4CPP}/include)
target_link_libraries(esajpip PRIVATE
    PkgConfig::PKG
    config
    log4cpp
    Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(esajpip PRIVATE
        _PLATFORM_LINUX
        _POSIX_C_SOURCE=200112L
        _XOPEN_SOURCE=600)
    target_link_libraries(esajpip PRIVATE rt)
elseif(APPLE)
    target_link_libraries(esajpip PRIVATE c++abi)
endif()

#add_executable(packet_information ${ESAJPIP_TOP}/packet_information.cc ${APP_SRCS} ${HTTP_SRCS} ${JPIP_SRCS} ${NET_SRCS} ${DATA_SRCS} ${JPEG2000_SRCS} ${ESAJPIP_TOP}/trace.cc)
#target_link_libraries(packet_information ${PKG_LIBRARIES} config log4cpp pthread)

install(TARGETS esajpip DESTINATION server/esajpip)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/server.cfg DESTINATION server/esajpip)

if(BUILD_TESTING)
    add_executable(protocol_test
        tests/protocol_test.cc
        ${ESAJPIP_TOP}/data/file_segment.cc
        ${ESAJPIP_TOP}/http/header.cc
        ${ESAJPIP_TOP}/http/protocol.cc
        ${ESAJPIP_TOP}/http/request.cc
        ${ESAJPIP_TOP}/http/response.cc
        ${ESAJPIP_TOP}/jpeg2000/coding_parameters.cc
        ${ESAJPIP_TOP}/jpip/databin_writer.cc
        ${ESAJPIP_TOP}/jpip/jpip.cc
        ${ESAJPIP_TOP}/jpip/request.cc
        ${ESAJPIP_TOP}/trace.cc)
    set_target_properties(protocol_test PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)
    target_compile_definitions(protocol_test PRIVATE _NO_READPROC NO_COLORS LOG4CPP_HAVE_SSTREAM)
    target_include_directories(protocol_test PRIVATE
        ${ESAJPIP_TOP}
        ${CMAKE_BINARY_DIR}/log4cpp/include
        ${LOG4CPP}/include)
    target_link_libraries(protocol_test PRIVATE log4cpp Threads::Threads)
    add_test(NAME protocol COMMAND protocol_test)
endif()
