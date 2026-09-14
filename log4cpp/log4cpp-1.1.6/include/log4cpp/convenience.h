/*
 * Copyright 2002, LifeLine Networks BV (www.lifeline.nl). All rights reserved.
 * Copyright 2002, Bastiaan Bakker. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 * @deprecated
 */

#ifndef LOG4CPP_CONVENIENCE_H
#define LOG4CPP_CONVENIENCE_H

#define LOG4CPP_LOGGER(name) \
  static log4cpp::Category& logger = log4cpp::Category::getInstance( name );

#define LOG4CPP_LOGGER_N(var_name, name) \
  static log4cpp::Category& var_name = log4cpp::Category::getInstance( name );

#endif

