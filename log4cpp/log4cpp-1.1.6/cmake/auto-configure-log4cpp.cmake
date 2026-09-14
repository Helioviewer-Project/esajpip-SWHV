# auto-configure-log4cpp.cmake
# Generate include/log4cpp/config.h from config.h.in (Autotools template)
# emulates behaviour of autotools
# expects patterns #undef MACRO_CAPABILITY on a single line

# --- Paths ---
set(CONFIG_IN   ${LOG4CPP}/include/config.h.in)
set(CONFIG_OUT  ${CMAKE_CURRENT_BINARY_DIR}/include/log4cpp/config.h)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/log4cpp)

# --- C++Macro prefix ---
set(PREFIX "LOG4CPP_")

# --- Include CMake modules for feature checks ---
include(CheckIncludeFileCXX)
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckTypeSize)
include(CheckCXXSourceCompiles)
include(CheckLibraryExists)

# --- Header checks ---
check_include_file_cxx(unistd.h    HAVE_UNISTD_H)
check_include_file_cxx(stdint.h    HAVE_STDINT_H)
check_include_file_cxx(inttypes.h  HAVE_INTTYPES_H)
check_include_file_cxx(io.h        HAVE_IO_H)
check_include_file_cxx(stdio.h     HAVE_STDIO_H)
check_include_file_cxx(stdlib.h    HAVE_STDLIB_H)
check_include_file_cxx(strings.h   HAVE_STRINGS_H)
check_include_file_cxx(string.h    HAVE_STRING_H)
check_include_file_cxx(sys/stat.h  HAVE_SYS_STAT_H)
check_include_file_cxx(sys/types.h HAVE_SYS_TYPES_H)
check_include_file_cxx(dlfcn.h     HAVE_DLFCN_H)

# --- Function checks ---
check_function_exists(ftime          HAVE_FTIME)
check_function_exists(gettimeofday  HAVE_GETTIMEOFDAY)
check_function_exists(localtime_r   HAVE_LOCALTIME_R)
check_function_exists(syslog        HAVE_SYSLOG)
check_function_exists(snprintf      HAVE_SNPRINTF)

# --- C++ feature checks ---
check_cxx_source_compiles("
namespace test {}
int main() { return 0; }
" HAVE_NAMESPACES)

check_cxx_source_compiles("
#include <sstream>
int main() { std::stringstream s; return 0; }
" HAVE_SSTREAM)

# --- Type checks ---
check_type_size(int64_t HAVE_INT64_T)

# --- Threads ---
find_package(Threads REQUIRED)
if(Threads_FOUND)
    set(HAVE_PTHREAD 1)
    set(USE_PTHREADS 1)
    set(HAVE_THREADING 1)
endif()

# --- Optional libraries ---
check_library_exists(idsa idsa_open "" HAVE_LIBIDSA)

# --- Package metadata ---
set(PACKAGE            "log4cpp")
set(PACKAGE_NAME       "log4cpp")
set(PACKAGE_VERSION    "${LOG4CPP_VERSION}")
set(PACKAGE_STRING     "log4cpp ${LOG4CPP_VERSION}")
set(PACKAGE_TARNAME    "log4cpp")
set(PACKAGE_URL        "https://log4cpp.sourceforge.net")
set(PACKAGE_BUGREPORT  "")
set(DISABLE_REMOTE_SYSLOG 0)
set(DISABLE_SMTP           0)

# Read source file line by line
file(STRINGS "${CONFIG_IN}" INPUT_LINES)

set(OUTPUT "")
set(PREFIX "LOG4CPP_")

string(APPEND OUTPUT
"#ifndef _INCLUDE_LOG4CPP_CONFIG_H\n")
string(APPEND OUTPUT
"#define _INCLUDE_LOG4CPP_CONFIG_H 1\n")

foreach(LINE IN LISTS INPUT_LINES)

    # Match a pure '#undef MACRO' line
    if(LINE MATCHES "^#undef[ \t]+([A-Z0-9_]+)[ \t]*$")
        set(MACRO "${CMAKE_MATCH_1}")

        if(DEFINED ${MACRO} AND NOT "${${MACRO}}" STREQUAL ""  AND NOT "${${MACRO}}" STREQUAL "0")
            # If value looks like a number, emit directly
            if("${${MACRO}}" MATCHES "^[0-9]+$")
                set(MACRO_VALUE "${${MACRO}}")
            else()
                # Otherwise, wrap string in quotes
                set(MACRO_VALUE "\"${${MACRO}}\"")
            endif()

            string(APPEND OUTPUT
                    "#ifndef ${PREFIX}${MACRO}\n"
                    "#define ${PREFIX}${MACRO} ${MACRO_VALUE}\n"
                    "#endif\n"
            )
        else()
            string(APPEND OUTPUT
                    "/* #undef ${PREFIX}${MACRO} */\n"
            )
        endif()

    else()
        # Emit original line exactly, plus newline
        string(APPEND OUTPUT "${LINE}\n")
    endif()

endforeach()
string(APPEND OUTPUT
        "/* _INCLUDE_LOG4CPP_CONFIG_H */\n")
string(APPEND OUTPUT
        "#endif\n")

# Write output
file(WRITE "${CONFIG_OUT}" "${OUTPUT}")

message(STATUS "Generated ${CONFIG_OUT}")
