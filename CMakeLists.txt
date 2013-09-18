
project(esajpip)

set(CMAKE_BUILD_TYPE RelWithDebInfo)

set(LIBCONFIG ${CMAKE_CURRENT_SOURCE_DIR}/libconfig/libconfig-1.4.9)
set(LOG4CPP   ${CMAKE_CURRENT_SOURCE_DIR}/log4cpp/log4cpp-1.1)

#for LOG4CPP
add_definitions(-pthread -DNDEBUG -DLOG4CPP_HAVE_SSTREAM)

add_subdirectory(libconfig)
add_subdirectory(log4cpp)
add_subdirectory(esajpip)
