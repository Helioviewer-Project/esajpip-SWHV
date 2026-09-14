/*
 * Copyright 2002, Log4cpp Project. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#include <sstream>
#include <log4cpp/threading/Threading.hh>

#if defined(LOG4CPP_HAVE_THREADING) && defined(LOG4CPP_USE_PTHREADS)

namespace log4cpp {
    namespace threading {

        std::string getThreadId() {
            std::ostringstream id;
            id << pthread_self();
            return id.str();
        }

    } // namespace threading
} // namespace log4cpp

#endif // LOG4CPP_HAVE_THREADING && LOG4CPP_USE_PTHREADS
