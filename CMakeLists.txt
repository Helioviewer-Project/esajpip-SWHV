
project(esajpip)

set(CMAKE_BUILD_TYPE RelWithDebInfo)

#set(CMAKE_CXX_COMPILER $ENV{LOCAL}/clang+llvm-3.5.0-macosx-apple-darwin/bin/clang++)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE -fno-omit-frame-pointer -W -Wextra -Wno-unused-parameter")
#-fsanitize=address,undefined

set(LIBCONFIG ${CMAKE_CURRENT_SOURCE_DIR}/libconfig/libconfig-1.4.9)
set(LOG4CPP   ${CMAKE_CURRENT_SOURCE_DIR}/log4cpp/log4cpp-1.1)

add_definitions(-pthread -DNDEBUG)

add_subdirectory(libconfig)
add_subdirectory(log4cpp)
add_subdirectory(esajpip)
