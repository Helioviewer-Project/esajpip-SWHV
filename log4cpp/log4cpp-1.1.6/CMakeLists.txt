# CMakeLists.txt
#
# Nicholas Yue nicholas_yue@users.sourceforge.net
# Alexander Perepelkin sanchouss@users.sourceforge.net
#
# Note:
# (1) A (dummy) file include/log4cpp/config.h is required (normally produced by Autotools via the ./configure step)
# (1.1) include/log4cpp/config.h is generated from include/config.h.in, emulating Autotools behaviour
# (2) Default installation directory is /usr/local, override with -DCMAKE_INSTALL_PREFIX="" during cmake
#     invocation
# (3) Do the usual "make clean all" to build the library
# (4) To install either "make install" or "make install DESTDIR=<your directory>"
# (5) Need to include changes in include/log4cpp/Portability.hh for OSX to build
#

CMAKE_MINIMUM_REQUIRED ( VERSION 3.10 )
PROJECT ( LOG4CPP VERSION 1.1.6 LANGUAGES C CXX )

INCLUDE ( GNUInstallDirs )
INCLUDE ( CMakePackageConfigHelpers )

SET ( LOG4CPP_LIBRARY_NAME "log4cpp" )
#SET ( CMAKE_VERBOSE_MAKEFILE ON )

IF ( CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR )
  SET ( LOG4CPP_IS_TOP_LEVEL ON )
ELSE ()
  SET ( LOG4CPP_IS_TOP_LEVEL OFF )
ENDIF ()

FIND_PACKAGE ( Threads REQUIRED )

IF (NOT CMAKE_BUILD_TYPE)
  SET ( CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." )
ENDIF ()

OPTION ( LOG4CPP_BUILD_EXAMPLES "Build log4cpp examples" OFF )

# for macOS use var APPLE
IF (WIN32)
  SET ( CMAKE_DEBUG_POSTFIX "D" )
  IF (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    SET ( WIN_DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX} )
  ENDIF ()
ENDIF (WIN32)

# produce include/log4cpp/config.h from include/config.h.in as autotools do
include(cmake/auto-configure-log4cpp.cmake)

SET (LOG4CPP_LIBRARY_SOURCES
  src/Appender.cpp
  src/AppenderSkeleton.cpp
  src/AppendersFactory.cpp
  src/BufferingAppender.cpp
  src/FactoryParams.cpp
  src/LayoutsFactory.cpp
  src/LevelEvaluator.cpp
  src/Localtime.cpp
  src/PassThroughLayout.cpp
  src/TriggeringEventEvaluatorFactory.cpp
  src/LayoutAppender.cpp
  src/FileAppender.cpp
  src/DailyRollingFileAppender.cpp
  src/RollingFileAppender.cpp
  src/FixedContextCategory.cpp
  src/IdsaAppender.cpp
  src/OstreamAppender.cpp
  src/StringQueueAppender.cpp
  src/SyslogAppender.cpp
  src/RemoteSyslogAppender.cpp
  src/SimpleLayout.cpp
  src/BasicLayout.cpp
  src/PatternLayout.cpp
  src/Category.cpp
  src/CategoryStream.cpp
  src/HierarchyMaintainer.cpp
  src/Configurator.cpp
  src/BasicConfigurator.cpp
  src/SimpleConfigurator.cpp
  src/PropertyConfigurator.cpp
  src/PropertyConfiguratorImpl.cpp
  src/LoggingEvent.cpp
  src/Priority.cpp
  src/NDC.cpp
  src/Filter.cpp
  src/TimeStamp.cpp
  src/StringUtil.cpp
  src/Properties.cpp
  src/Win32DebugAppender.cpp
  src/NTEventLogAppender.cpp
  src/DllMain.cpp
  src/DummyThreads.cpp
  src/MSThreads.cpp
  src/OmniThreads.cpp
  src/PThreads.cpp
  src/PortabilityImpl.cpp
  src/AbortAppender.cpp
)

# Respect builtin CMake variable BUILD_SHARED_LIBS when it is specified. Otherwise build both static and shared libs
IF (NOT DEFINED BUILD_SHARED_LIBS)
  SET ( LOG4CPP_BUILD_SHARED ON )
  SET ( LOG4CPP_BUILD_STATIC ON )
ELSE ()
  IF (BUILD_SHARED_LIBS)
    SET ( LOG4CPP_BUILD_SHARED ON )
    SET ( LOG4CPP_BUILD_STATIC OFF )
  ELSE ()
    SET ( LOG4CPP_BUILD_SHARED OFF )
    SET ( LOG4CPP_BUILD_STATIC ON )
  ENDIF ()
ENDIF ()

MESSAGE ( "LOG4CPP_BUILD_SHARED is ${LOG4CPP_BUILD_SHARED}; LOG4CPP_BUILD_STATIC is ${LOG4CPP_BUILD_STATIC}" )
SET ( LOG4CPP_INSTALL_TARGETS "" )

# Outer variables are used to set target properties inside the macro
MACRO ( CREATE_LOG4CPP_LIB TARGET_NAME LIB_TYPE OUTPUT_NAME )
  ADD_LIBRARY ( ${TARGET_NAME} ${LIB_TYPE} ${LOG4CPP_LIBRARY_SOURCES} )
  TARGET_LINK_LIBRARIES ( ${TARGET_NAME} PRIVATE Threads::Threads )
  LIST ( APPEND LOG4CPP_INSTALL_TARGETS ${TARGET_NAME} )
  SET_TARGET_PROPERTIES ( ${TARGET_NAME} PROPERTIES OUTPUT_NAME ${OUTPUT_NAME} )
  TARGET_INCLUDE_DIRECTORIES ( ${TARGET_NAME}
          PUBLIC
          $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
          $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  IF (WIN32)
    TARGET_COMPILE_DEFINITIONS ( ${TARGET_NAME} PRIVATE WIN32 )
    TARGET_LINK_LIBRARIES ( ${TARGET_NAME} PRIVATE kernel32 user32 ws2_32 advapi32 msvcrt${WIN_DEBUG_POSTFIX} )
    #SET_TARGET_PROPERTIES ( ${TARGET_NAME} PROPERTIES LINK_FLAGS /NODEFAULTLIB:msvcrt${WIN_DEBUG_POSTFIX} )
    SET_TARGET_PROPERTIES ( ${TARGET_NAME} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX} )
  ELSE (WIN32)
    TARGET_COMPILE_DEFINITIONS ( ${TARGET_NAME} PRIVATE LOG4CPP_HAVE_SSTREAM )
  ENDIF (WIN32)
  TARGET_COMPILE_DEFINITIONS ( ${TARGET_NAME} PRIVATE $<$<CONFIG:Debug>:_DEBUG DEBUG LOG4CPP_FIX_ERROR_COLLISION> )
  TARGET_COMPILE_DEFINITIONS ( ${TARGET_NAME} PRIVATE $<$<CONFIG:Release>:NDEBUG> )
ENDMACRO ()

IF (LOG4CPP_BUILD_STATIC)
  IF (WIN32)
    # win will also build small .lib even when shared library is requested, this is to avoid name clash
    SET ( LOG4CPP_STATIC_OUTPUT_NAME ${LOG4CPP_LIBRARY_NAME}_static )
  ELSE ()
    SET ( LOG4CPP_STATIC_OUTPUT_NAME ${LOG4CPP_LIBRARY_NAME} )
  ENDIF ()
  CREATE_LOG4CPP_LIB ( log4cpp_static STATIC ${LOG4CPP_STATIC_OUTPUT_NAME} )
  ADD_LIBRARY ( log4cpp::log4cpp_static ALIAS log4cpp_static )
  ADD_LIBRARY ( LOG4CPP_STATIC ALIAS log4cpp_static )
ENDIF (LOG4CPP_BUILD_STATIC)

IF (LOG4CPP_BUILD_SHARED)
   CREATE_LOG4CPP_LIB ( log4cpp_shared SHARED ${LOG4CPP_LIBRARY_NAME} )
   ADD_LIBRARY ( log4cpp::log4cpp_shared ALIAS log4cpp_shared )
   ADD_LIBRARY ( LOG4CPP_SHARED ALIAS log4cpp_shared )
   IF (WIN32)
    TARGET_COMPILE_DEFINITIONS ( log4cpp_shared PRIVATE _CRT_SECURE_NO_WARNINGS LOG4CPP_HAS_DLL LOG4CPP_BUILD_DLL )
   ENDIF ()
ENDIF (LOG4CPP_BUILD_SHARED)

IF (WIN32)
  # RAD Studio specifics
  IF (CMAKE_CXX_COMPILER MATCHES ".*bcc32c.*" OR CMAKE_CXX_COMPILER MATCHES ".*bcc64x.*")
    MESSAGE ( STATUS "Embarcadero C++Builder detected" )
    SET ( WIN_DEBUG_POSTFIX "" )
    SET_EMBT_TARGET ( log4cpp_static RTL )
    SET_EMBT_TARGET ( log4cpp_shared RTL )
  ENDIF ()
ENDIF (WIN32)

INSTALL (
  DIRECTORY include/log4cpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
#  header Portability.hh will include "log4cpp/config.h" on linux, need to have it existing
#  PATTERN "config.h" EXCLUDE
  PATTERN ".git" EXCLUDE
  PATTERN ".svn" EXCLUDE
  PATTERN "*.am" EXCLUDE
  PATTERN "*.in" EXCLUDE
  )

INSTALL (
# "log4cpp/config.h" is auto generated by cmake
  FILES "${CMAKE_CURRENT_BINARY_DIR}/include/log4cpp/config.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/log4cpp"
  )

INSTALL (
  TARGETS ${LOG4CPP_INSTALL_TARGETS}
  EXPORT log4cppTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

INSTALL (
  EXPORT log4cppTargets
  NAMESPACE log4cpp::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/log4cpp
)

CONFIGURE_PACKAGE_CONFIG_FILE (
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/log4cppConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/log4cppConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/log4cpp
)

WRITE_BASIC_PACKAGE_VERSION_FILE (
  ${CMAKE_CURRENT_BINARY_DIR}/log4cppConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

INSTALL (
  FILES
  ${CMAKE_CURRENT_BINARY_DIR}/log4cppConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/log4cppConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/log4cpp
)

ENABLE_TESTING()
ADD_SUBDIRECTORY ( tests )
IF ( LOG4CPP_BUILD_EXAMPLES AND LOG4CPP_IS_TOP_LEVEL )
  ADD_SUBDIRECTORY ( examples )
ENDIF ()
