
project(esajpip)

set(CMAKE_BUILD_TYPE RelWithDebInfo)

#set(CMAKE_CXX_COMPILER $ENV{LOCAL}/clang+llvm-3.5.0-macosx-apple-darwin/bin/clang)
#set(CMAKE_C_COMPILER   $ENV{LOCAL}/clang+llvm-3.5.0-macosx-apple-darwin/bin/clang)
set(EXTRA_FLAGS     "-fPIE -fno-omit-frame-pointer -W -Wextra -Wno-unused-parameter")
#-fsanitize=address,undefined
#-fsanitize=memory -fsanitize-memory-track-origins"
#-fsanitize=thread

set(CMAKE_C_FLAGS_RELWITHDEBINFO   "${CMAKE_C_FLAGS_RELWITHDEBINFO}   ${EXTRA_FLAGS}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${EXTRA_FLAGS}")

set(LIBCONFIG ${CMAKE_CURRENT_SOURCE_DIR}/libconfig/libconfig-1.4.9)
set(LOG4CPP   ${CMAKE_CURRENT_SOURCE_DIR}/log4cpp/log4cpp-1.1)

add_definitions(-pthread -DNDEBUG)

add_subdirectory(libconfig)
add_subdirectory(log4cpp)
add_subdirectory(esajpip)
