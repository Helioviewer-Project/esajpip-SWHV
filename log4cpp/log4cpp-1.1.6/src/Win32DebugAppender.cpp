/*
 * Win32DebugAppender.cpp
 *
 * Copyright 2002, the Log4cpp project.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifdef WIN32 // only available on Win32
// clang-format off
#include "PortabilityImpl.hh"
#ifdef LOG4CPP_HAVE_IO_H
#    include <io.h>
#endif

// order of windows headers matters, avoid reordering here
#include <windows.h>
#include <winbase.h>
// clang-format on

#include "log4cpp/Win32DebugAppender.hh"
#include "log4cpp/Category.hh"
#include <log4cpp/FactoryParams.hh>
#include <memory>
#include <stdio.h>

namespace log4cpp {

    Win32DebugAppender::Win32DebugAppender(const std::string& name) : LayoutAppender(name) {}

    Win32DebugAppender::~Win32DebugAppender() {
        close();
    }

    void Win32DebugAppender::close() {}

    void Win32DebugAppender::_append(const LoggingEvent& event) {
        std::string message(_getLayout().format(event));
        ::OutputDebugString(message.c_str());
    }

    std::LOG4CPP_UNIQUE_PTR<Appender> create_win32_debug_appender(const FactoryParams& params) {
        std::string name;
        params.get_for("win32 debug appender").required("name", name);

        return std::LOG4CPP_UNIQUE_PTR<Appender>(new Win32DebugAppender(name));
    }
} // namespace log4cpp

#endif // WIN32
