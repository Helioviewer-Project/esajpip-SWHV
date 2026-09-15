# examples/CMakeLists.txt
# Forward building to subdirectories if examples are enabled

if(LOG4CPP_BUILD_EXAMPLES)
    add_subdirectory(cmake_usages)
endif()
