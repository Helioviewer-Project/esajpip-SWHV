# Server runtime and filesystem adapter.
add_library(esajpip_server STATIC
    config.cc
    storage/file_manager.cc
    channel_engine.cc
    channel_work.cc
    http/connection.cc
    http/request_head.cc
    server.cc
    trace.cc)
esajpip_configure_target(esajpip_server)
target_include_directories(esajpip_server PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(esajpip_server PUBLIC
    jpip
    PkgConfig::PKG
    PkgConfig::LLHTTP
    PkgConfig::LIBUV
    ZLIB::ZLIB
    Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(esajpip_server PRIVATE
        _PLATFORM_LINUX
        _POSIX_C_SOURCE=200112L
        _XOPEN_SOURCE=600)
elseif(APPLE)
    target_link_libraries(esajpip_server PUBLIC c++abi)
endif()

add_executable(esajpip main.cc)
esajpip_configure_target(esajpip)
target_link_libraries(esajpip PRIVATE esajpip_server)
# Keep the executable beside the configured server.ini in the build root.
set_target_properties(esajpip PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
