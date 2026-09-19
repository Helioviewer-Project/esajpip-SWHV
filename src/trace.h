#ifndef _ESAJPIP_TRACE_H_
#define _ESAJPIP_TRACE_H_

#include <iostream>
#include <sstream>
#include <string>

namespace trace {

bool Initialize(const std::string &file_name);
bool Enabled();
void Flush();
void Drain();
void Write(const std::string &message);

}

#define LOG(a)                                                                  \
    do {                                                                        \
        if (trace::Enabled()) {                                                  \
            std::ostringstream log_message;                                     \
            log_message << a;                                                   \
            trace::Write(log_message.str());                                    \
        }                                                                       \
    } while (false)

#define ERROR(a)                                                                \
    do {                                                                        \
        if (trace::Enabled()) {                                                  \
            std::ostringstream log_message;                                     \
            log_message << __FILE__ << ":" << __LINE__ << ": ERROR: " << a;     \
            trace::Write(log_message.str());                                    \
        }                                                                       \
    } while (false)

#if defined(SHOW_TRACES) && !defined(NDEBUG)
#define TRACE(a)                                                                \
    do {                                                                        \
        std::ostringstream log_message;                                         \
        log_message << __FILE__ << ":" << __LINE__ << ": TRACE: " << a;         \
        trace::Write(log_message.str());                                        \
    } while (false)
#else
#define TRACE(a) do { } while (false)
#endif /* SHOW_TRACES && !NDEBUG */

#define CERR(a) (std::cerr << a << std::endl, -1)

#endif /* _ESAJPIP_TRACE_H_ */
