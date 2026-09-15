#
# Note:
# Example builds two targets, each against either shared or static log4cpp library.
# If only one of them is available, linking to another one should be removed.
#
cmake_minimum_required(VERSION 3.10)
project(log4cpp_as_package_example LANGUAGES CXX)

# Determine if this example is being built standalone and not being included from top-level CMake as one of examples
get_directory_property(HAS_PARENT PARENT_DIRECTORY)
if(NOT HAS_PARENT)
    set(EXAMPLE_BUILT_ALONE ON)
else()
    set(EXAMPLE_BUILT_ALONE OFF)
endif()

# If built standalone, find the (already) installed log4cpp package
if(EXAMPLE_BUILT_ALONE)
    # Find log4cpp installed via CMake export files
    find_package(log4cpp REQUIRED)
endif()

# Create executable linked to shared library
add_executable(log4cpp_as_package_example log4cpp_as_package.cpp)
# Create executable with static library
add_executable(log4cpp_as_package_example_static log4cpp_as_package.cpp)

target_compile_features(log4cpp_as_package_example PUBLIC cxx_std_17)
target_compile_features(log4cpp_as_package_example_static PUBLIC cxx_std_17)

# Link 1st exe against log4cpp shared library
target_link_libraries(log4cpp_as_package_example PRIVATE log4cpp::log4cpp_shared)
# Link 2nd exe against log4cpp static library
target_link_libraries(log4cpp_as_package_example_static PRIVATE log4cpp::log4cpp_static)

# when linking to log4cpp::log4cpp_static, link to threads shared lib too
find_package(Threads REQUIRED)
target_link_libraries(log4cpp_as_package_example_static PRIVATE Threads::Threads)

file(COPY ${PROJECT_SOURCE_DIR}/resources
#        DESTINATION ${CMAKE_BINARY_DIR})
        DESTINATION .)
