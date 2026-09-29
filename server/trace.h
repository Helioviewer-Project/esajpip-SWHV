#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace server {
namespace trace {

bool Initialize(const std::string &file_name);
bool Enabled();
void Flush();
void Drain();
void Write(const std::string &message);

}
} // namespace server

#define LOG(a)                                                                  \
    do {                                                                        \
        if (::server::trace::Enabled()) {                                       \
            std::ostringstream log_message;                                     \
            log_message << a;                                                   \
            ::server::trace::Write(log_message.str());                          \
        }                                                                       \
    } while (false)

#define ERROR(a)                                                                \
    do {                                                                        \
        if (::server::trace::Enabled()) {                                       \
            std::ostringstream log_message;                                     \
            log_message << __FILE__ << ":" << __LINE__ << ": ERROR: " << a;     \
            ::server::trace::Write(log_message.str());                          \
        }                                                                       \
    } while (false)

#if defined(SHOW_TRACES) && !defined(NDEBUG)
#define TRACE(a)                                                                \
    do {                                                                        \
        std::ostringstream log_message;                                         \
        log_message << __FILE__ << ":" << __LINE__ << ": TRACE: " << a;         \
        ::server::trace::Write(log_message.str());                              \
    } while (false)
#else
#define TRACE(a) do { } while (false)
#endif /* SHOW_TRACES && !NDEBUG */

#define CERR(a) (std::cerr << a << std::endl, -1)
