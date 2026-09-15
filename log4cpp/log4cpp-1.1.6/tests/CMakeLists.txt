# CMakeLists.txt
#
# Alexander Perepelkin sanchouss@users.sourceforge.net
#
# Builds tests for log4cpp.
# Collects all test source files in this directory and builds them against the log4cpp library and threading support.
# Tests can be executed using CTest.

cmake_minimum_required(VERSION 3.10)

project(log4cpp_tests LANGUAGES CXX)

if(NOT TARGET log4cpp::log4cpp_shared)
    message(STATUS "log4cpp::log4cpp_shared target not defined")
endif()

# pthread is needed on POSIX for threading
find_package(Threads REQUIRED)

# There are few CppUnit based test cases to include yet:
#find_package(CppUnit REQUIRED)

# Gather test sources
file(GLOB TEST_EXECUTABLE_SOURCES "test*.cpp")
# Remove CppUnit-based file for now
list(REMOVE_ITEM TEST_EXECUTABLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/testNDCMain.cpp")

if(WIN32)
    list(REMOVE_ITEM TEST_EXECUTABLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/testbench.cpp")
else()
    list(REMOVE_ITEM TEST_EXECUTABLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/testNTEventLog.cpp")
endif()

# Helper sources (with no main function)
set(TEST_AUX_SOURCES)

if(NOT WIN32)
list(APPEND TEST_AUX_SOURCES
    Clock.cpp
)
endif()

if(TEST_EXECUTABLE_SOURCES)
    message(STATUS "Found test source files: ${TEST_EXECUTABLE_SOURCES}")
else()
    message(WARNING "No test sources found in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(TARGET log4cpp::log4cpp_shared)
    set(LOG4CPP_TEST_LIB log4cpp::log4cpp_shared)
elseif(TARGET log4cpp::log4cpp_static)
    set(LOG4CPP_TEST_LIB log4cpp::log4cpp_static)
else()
    message(FATAL_ERROR "No log4cpp::log4cpp_shared or log4cpp::log4cpp_static library target found")
endif()

# Build each test as an executable
foreach(test_source IN LISTS TEST_EXECUTABLE_SOURCES)
    # Remove extension to form target name
    get_filename_component(test_name ${test_source} NAME_WE)

    add_executable(${test_name} ${test_source} ${TEST_AUX_SOURCES})

    # include src dir for "Properties.hh"
    TARGET_INCLUDE_DIRECTORIES ( ${test_name}
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../src>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    # Link against log4cpp (shared or static) and threads; includes are added automatically
    target_link_libraries(${test_name} PRIVATE ${LOG4CPP_TEST_LIB}  Threads::Threads)

    # Add test to CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    # Add path to the shared library into PATH for the test on windows so CTest can run it
    if(WIN32)
        set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:${LOG4CPP_TEST_LIB}>;$ENV{PATH}")
    endif()
endforeach()

# List of resource files to copy
set(TEST_RESOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/testSimpleConfig.log4cpp.init"
        "${CMAKE_CURRENT_SOURCE_DIR}/testPropConfig.log4cpp.nt.properties"
        "${CMAKE_CURRENT_SOURCE_DIR}/log4cpp.properties"
        "${CMAKE_CURRENT_SOURCE_DIR}/testPropConfig.log4cpp.properties"
        "${CMAKE_CURRENT_SOURCE_DIR}/testDailyRollingFileAppender.log4cpp.properties"
        "${CMAKE_CURRENT_SOURCE_DIR}/testDailyRollingFileAppender.log4cpp.nt.properties"
        "${CMAKE_CURRENT_SOURCE_DIR}/testPropertyConfig.log4cpp.properties"
        "${CMAKE_CURRENT_SOURCE_DIR}/testProperties.properties"
)

# Directory where resources will be copied
set(TEST_RESOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# Copy all resources at once
add_custom_target(copy_test_resources ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${TEST_RESOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TEST_RESOURCE_FILES}
        ${TEST_RESOURCE_DIR}
        # Copy directory nesteddir
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/nesteddir
        ${TEST_RESOURCE_DIR}/nesteddir
        COMMENT "Copying test resource files to ${TEST_RESOURCE_DIR}"
)
