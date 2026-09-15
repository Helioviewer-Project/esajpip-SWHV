#
# Note:
# Example builds two targets, each against either shared or static log4cpp library.
# If only one of them is available, linking to another one should be removed.
#
cmake_minimum_required(VERSION 3.10)
project(log4cpp_as_subproject_example LANGUAGES CXX)

# Ensure shared libraries are built by subprojects
#set(BUILD_SHARED_LIBS ON CACHE BOOL "")

# Determine if this example is being built standalone and not being included from top-level CMake as one of examples
get_directory_property(HAS_PARENT PARENT_DIRECTORY)
if(NOT HAS_PARENT)
    set(EXAMPLE_BUILT_ALONE ON)
else()
    set(EXAMPLE_BUILT_ALONE OFF)
endif()

# If built standalone, add the project log4cpp as a subproject (subdirectory)
if(EXAMPLE_BUILT_ALONE)
    add_subdirectory(../../.. log4cpp-build)
endif()

# Sanity check: we require shared log4cpp
#if(NOT TARGET log4cpp::log4cpp_shared)
#    message(FATAL_ERROR "log4cpp::log4cpp_shared target not available")
#endif()

# Sanity check: we require static log4cpp
#if(NOT TARGET log4cpp::log4cpp_static)
#    message(FATAL_ERROR "log4cpp::log4cpp_static target not available")
#endif()

# Create executable linked to shared library
add_executable(log4cpp_as_subproject_example log4cpp_as_subproject.cpp)
# Create executable with static library
add_executable(log4cpp_as_subproject_example_static log4cpp_as_subproject.cpp)

target_compile_features(log4cpp_as_subproject_example PUBLIC cxx_std_17)
target_compile_features(log4cpp_as_subproject_example_static PUBLIC cxx_std_17)

# Link 1st exe against log4cpp shared library
target_link_libraries(log4cpp_as_subproject_example PRIVATE log4cpp::log4cpp_shared)
# Link 2nd exe against log4cpp static library
target_link_libraries(log4cpp_as_subproject_example_static PRIVATE log4cpp::log4cpp_static)

file(COPY ${PROJECT_SOURCE_DIR}/resources
        #        DESTINATION ${CMAKE_BINARY_DIR})
        DESTINATION .)
