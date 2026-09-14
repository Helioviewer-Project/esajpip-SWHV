/*
 * OstreamAppender.cpp
 *
 * Copyright 2000, LifeLine Networks BV (www.lifeline.nl). All rights reserved.
 * Copyright 2000, Bastiaan Bakker. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#include "PortabilityImpl.hh"
#ifdef LOG4CPP_HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <log4cpp/OstreamAppender.hh>
#include <sys/stat.h>
#include <sys/types.h>

namespace log4cpp {

    OstreamAppender::OstreamAppender(const std::string& name, std::ostream* stream)
        : LayoutAppender(name), _stream(stream) {}

    OstreamAppender::~OstreamAppender() {
        close();
    }

    void OstreamAppender::close() {
        // empty
    }

    void OstreamAppender::_append(const LoggingEvent& event) {
        (*_stream) << _getLayout().format(event);
        if (!_stream->good()) {
            // XXX help! help!
        }
    }

    bool OstreamAppender::reopen() {
        return true;
    }
} // namespace log4cpp
