/*
 * PropertyConfigurator.cpp
 *
 * Copyright 2001, Glen Scott. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */
#include "PortabilityImpl.hh"
#include "PropertyConfiguratorImpl.hh"
#include <log4cpp/PropertyConfigurator.hh>

namespace log4cpp {

    void PropertyConfigurator::configure(const std::string& initFileName) {
        static PropertyConfiguratorImpl configurator;

        configurator.doConfigure(initFileName);
    }
} // namespace log4cpp
