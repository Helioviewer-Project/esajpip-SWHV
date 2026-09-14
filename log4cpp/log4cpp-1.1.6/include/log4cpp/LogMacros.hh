/*
 * LogMacros.hh
 *
 * Copyright 2026, Alexander Perepelkin. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifndef LOG4CPP_LOGMACROS_H
#define LOG4CPP_LOGMACROS_H

#include <log4cpp/Category.hh>
#include <sstream>

/**
 * @file LogMacros.hh
 * @brief Optional macro wrappers for loggers, include formatted and streaming style
 *
 * Define `LOG4CPP_ACTIVE_LEVEL` as one of the LOG4CPP_PRIORITY_* macros for compile-time
 * stripping of lower-priority logging calls. The #define directive must precede the inclusion of this header file.
 * All log macros with priority level lower than value of `LOG4CPP_ACTIVE_LEVEL`
 * will be removed at compile time resulting in zero runtime overhead.
 *
 * Formatting macros are not available on older compilers that lack variadic macro support.
 * In such cases, only macros accepting static messages are defined.
 *
 * @example
 * #define LOG4CPP_ACTIVE_LEVEL log4cpp::Priority::INFO
 * #include <log4cpp/LogMacros.hh>
 *
 * log4cpp::Category& logger = log4cpp::Category::getRoot();
 *
 * LOG4CPP_DEBUG(logger, "This debug log will be stripped at compile time");
 * LOG4CPP_INFO(logger,  "Application started");
 * LOG4CPP_WARN(logger,  "Low disk space: %d%% remaining", diskPercent);
 * LOG4CPP_ERROR(logger, "Failed to open file: %s", filename.c_str());
 *
 * LOG4CPP_INFO_S(logger, "Value=" << x << std::endl);
 */

// Possible values for LOG4CPP_ACTIVE_LEVEL.
// These preprocessor constants correspond to the log4cpp::Priority enum values.
#define LOG4CPP_PRIORITY_EMERG 0
#define LOG4CPP_PRIORITY_FATAL 0 // same as EMERG
#define LOG4CPP_PRIORITY_ALERT 100
#define LOG4CPP_PRIORITY_CRIT 200
#define LOG4CPP_PRIORITY_ERROR 300
#define LOG4CPP_PRIORITY_WARN 400
#define LOG4CPP_PRIORITY_NOTICE 500
#define LOG4CPP_PRIORITY_INFO 600
#define LOG4CPP_PRIORITY_DEBUG 700
#define LOG4CPP_PRIORITY_NOTSET 800

// Compile-time stripping
#ifdef LOG4CPP_ACTIVE_LEVEL
#if LOG4CPP_ACTIVE_LEVEL < LOG4CPP_PRIORITY_DEBUG
#define LOG4CPP_DISABLE_DEBUG
#define LOG4CPP_DISABLE_DEBUG_S
#endif
#if LOG4CPP_ACTIVE_LEVEL < LOG4CPP_PRIORITY_INFO
#define LOG4CPP_DISABLE_INFO
#define LOG4CPP_DISABLE_INFO_S
#endif
#if LOG4CPP_ACTIVE_LEVEL < LOG4CPP_PRIORITY_NOTICE
#define LOG4CPP_DISABLE_NOTICE
#define LOG4CPP_DISABLE_NOTICE_S
#endif
#if LOG4CPP_ACTIVE_LEVEL < LOG4CPP_PRIORITY_WARN
#define LOG4CPP_DISABLE_WARN
#define LOG4CPP_DISABLE_WARN_S
#endif
#if LOG4CPP_ACTIVE_LEVEL < LOG4CPP_PRIORITY_ERROR
#define LOG4CPP_DISABLE_ERROR
#define LOG4CPP_DISABLE_ERROR_S
#endif
#if LOG4CPP_ACTIVE_LEVEL < LOG4CPP_PRIORITY_CRIT
#define LOG4CPP_DISABLE_CRIT
#define LOG4CPP_DISABLE_CRIT_S
#endif
#if LOG4CPP_ACTIVE_LEVEL < LOG4CPP_PRIORITY_ALERT
#define LOG4CPP_DISABLE_ALERT
#define LOG4CPP_DISABLE_ALERT_S
#endif
#if LOG4CPP_ACTIVE_LEVEL < LOG4CPP_PRIORITY_FATAL
#define LOG4CPP_DISABLE_FATAL
#define LOG4CPP_DISABLE_FATAL_S
#endif
#if LOG4CPP_ACTIVE_LEVEL < LOG4CPP_PRIORITY_EMERG
#define LOG4CPP_DISABLE_EMERG
#define LOG4CPP_DISABLE_EMERG_S
#endif
#endif // LOG4CPP_ACTIVE_LEVEL

// Detect variadic macro support
#if defined(__cplusplus) && __cplusplus >= 201103L
#define LOG4CPP_VARIADIC_MACROS_SUPPORTED 1
#elif defined(_MSC_VER) && _MSC_VER >= 1400
#define LOG4CPP_VARIADIC_MACROS_SUPPORTED 1
#else
#define LOG4CPP_VARIADIC_MACROS_SUPPORTED 0
#endif

// Generic formatted logging
#if LOG4CPP_VARIADIC_MACROS_SUPPORTED
#define LOG4CPP_LOG(logger, priority, ...)                                                                             \
    do {                                                                                                               \
        if ((logger).isPriorityEnabled(priority)) {                                                                    \
            (logger).log(priority, __VA_ARGS__);                                                                       \
        }                                                                                                              \
    } while (0)
/**
 * @brief Core forwarding macro.
 *
 * Checks if the specified @p priority is enabled at runtime,
 * and forwards variadic arguments to `logger.log()`.
 *
 * @param logger The log4cpp::Category instance
 * @param priority The log4cpp::Priority level
 * @param ... Variadic arguments for formatting
 *
 * @example
 * LOG4CPP_LOG(logger, log4cpp::Priority::INFO, "User %d logged in", userId);
 */
#define LOG4CPP_LOG(logger, priority, ...)                                                                             \
    do {                                                                                                               \
        if ((logger).isPriorityEnabled(priority)) {                                                                    \
            (logger).log(priority, __VA_ARGS__);                                                                       \
        }                                                                                                              \
    } while (0)

// Level macros using DISABLE flags
#ifdef LOG4CPP_DISABLE_DEBUG
#define LOG4CPP_DEBUG(logger, ...) ((void)0)
#else
/**
 * @brief Logs a DEBUG-level message.
 *
 * @example
 * LOG4CPP_DEBUG(logger, "Debug value: %d", value);
 */
#define LOG4CPP_DEBUG(logger, ...) LOG4CPP_LOG(logger, log4cpp::Priority::DEBUG, __VA_ARGS__)
#endif

#ifdef LOG4CPP_DISABLE_INFO
#define LOG4CPP_INFO(logger, ...) ((void)0)
#else
/**
 * @brief Logs an INFO-level message.
 *
 * @example
 * LOG4CPP_INFO(logger, "Application started");
 */
#define LOG4CPP_INFO(logger, ...) LOG4CPP_LOG(logger, log4cpp::Priority::INFO, __VA_ARGS__)
#endif

#ifdef LOG4CPP_DISABLE_NOTICE
#define LOG4CPP_NOTICE(logger, ...) ((void)0)
#else
/**
 * @brief Logs a NOTICE-level message.
 *
 * @example
 * LOG4CPP_NOTICE(logger, "Configuration loaded successfully");
 */
#define LOG4CPP_NOTICE(logger, ...) LOG4CPP_LOG(logger, log4cpp::Priority::NOTICE, __VA_ARGS__)
#endif

#ifdef LOG4CPP_DISABLE_WARN
#define LOG4CPP_WARN(logger, ...) ((void)0)
#else
/**
 * @brief Logs a WARN-level message.
 *
 * @example
 * LOG4CPP_WARN(logger, "Low memory: %d MB remaining", memoryMb);
 */
#define LOG4CPP_WARN(logger, ...) LOG4CPP_LOG(logger, log4cpp::Priority::WARN, __VA_ARGS__)
#endif

#ifdef LOG4CPP_DISABLE_ERROR
#define LOG4CPP_ERROR(logger, ...) ((void)0)
#else
/**
 * @brief Logs an ERROR-level message.
 *
 * @example
 * LOG4CPP_ERROR(logger, "Failed to open file: %s", filename.c_str());
 */
#define LOG4CPP_ERROR(logger, ...) LOG4CPP_LOG(logger, log4cpp::Priority::ERROR, __VA_ARGS__)
#endif

#ifdef LOG4CPP_DISABLE_CRIT
#define LOG4CPP_CRIT(logger, ...) ((void)0)
#else
/**
 * @brief Logs a CRIT-level message.
 *
 * @example
 * LOG4CPP_CRIT(logger, "Critical error in module %s", moduleName.c_str());
 */
#define LOG4CPP_CRIT(logger, ...) LOG4CPP_LOG(logger, log4cpp::Priority::CRIT, __VA_ARGS__)
#endif

#ifdef LOG4CPP_DISABLE_ALERT
#define LOG4CPP_ALERT(logger, ...) ((void)0)
#else
/**
 * @brief Logs an ALERT-level message.
 *
 * @example
 * LOG4CPP_ALERT(logger, "Immediate attention required: %s", alertMsg.c_str());
 */
#define LOG4CPP_ALERT(logger, ...) LOG4CPP_LOG(logger, log4cpp::Priority::ALERT, __VA_ARGS__)
#endif

#ifdef LOG4CPP_DISABLE_FATAL
#define LOG4CPP_FATAL(logger, ...) ((void)0)
#else
/**
 * @brief Logs a FATAL-level message.
 *
 * @example
 * LOG4CPP_FATAL(logger, "Fatal error, shutting down");
 */
#define LOG4CPP_FATAL(logger, ...) LOG4CPP_LOG(logger, log4cpp::Priority::FATAL, __VA_ARGS__)
#endif

#ifdef LOG4CPP_DISABLE_EMERG
#define LOG4CPP_EMERG(logger, ...) ((void)0)
#else
/**
 * @brief Logs an EMERG-level message.
 *
 * @example
 * LOG4CPP_EMERG(logger, "System is unusable!");
 */
#define LOG4CPP_EMERG(logger, ...) LOG4CPP_LOG(logger, log4cpp::Priority::EMERG, __VA_ARGS__)
#endif

#else // LOG4CPP_VARIADIC_MACROS_SUPPORTED
//    Fallback for C++98: fixed-argument macros
#define LOG4CPP_LOG_NO_VA(logger, priority, msg)                                                                       \
    do {                                                                                                               \
        if ((logger).isPriorityEnabled(priority)) {                                                                    \
            (logger).log(priority, msg);                                                                               \
        }                                                                                                              \
    } while (0)

#ifdef LOG4CPP_DISABLE_DEBUG
#define LOG4CPP_DEBUG(logger, msg) ((void)0)
#else
#define LOG4CPP_DEBUG(logger, msg) LOG4CPP_LOG_NO_VA(logger, log4cpp::Priority::DEBUG, msg)
#endif

#ifdef LOG4CPP_DISABLE_INFO
#define LOG4CPP_INFO(logger, msg) ((void)0)
#else
#define LOG4CPP_INFO(logger, msg) LOG4CPP_LOG_NO_VA(logger, log4cpp::Priority::INFO, msg)
#endif

#ifdef LOG4CPP_DISABLE_NOTICE
#define LOG4CPP_NOTICE(logger, msg) ((void)0)
#else
#define LOG4CPP_NOTICE(logger, msg) LOG4CPP_LOG_NO_VA(logger, log4cpp::Priority::NOTICE, msg)
#endif

#ifdef LOG4CPP_DISABLE_WARN
#define LOG4CPP_WARN(logger, msg) ((void)0)
#else
#define LOG4CPP_WARN(logger, msg) LOG4CPP_LOG_NO_VA(logger, log4cpp::Priority::WARN, msg)
#endif

#ifdef LOG4CPP_DISABLE_ERROR
#define LOG4CPP_ERROR(logger, msg) ((void)0)
#else
#define LOG4CPP_ERROR(logger, msg) LOG4CPP_LOG_NO_VA(logger, log4cpp::Priority::ERROR, msg)
#endif

#ifdef LOG4CPP_DISABLE_CRIT
#define LOG4CPP_CRIT(logger, msg) ((void)0)
#else
#define LOG4CPP_CRIT(logger, msg) LOG4CPP_LOG_NO_VA(logger, log4cpp::Priority::CRIT, msg)
#endif

#ifdef LOG4CPP_DISABLE_ALERT
#define LOG4CPP_ALERT(logger, msg) ((void)0)
#else
#define LOG4CPP_ALERT(logger, msg) LOG4CPP_LOG_NO_VA(logger, log4cpp::Priority::ALERT, msg)
#endif

#ifdef LOG4CPP_DISABLE_FATAL
#define LOG4CPP_FATAL(logger, msg) ((void)0)
#else
#define LOG4CPP_FATAL(logger, msg) LOG4CPP_LOG_NO_VA(logger, log4cpp::Priority::FATAL, msg)
#endif

#ifdef LOG4CPP_DISABLE_EMERG
#define LOG4CPP_EMERG(logger, msg) ((void)0)
#else
#define LOG4CPP_EMERG(logger, msg) LOG4CPP_LOG_NO_VA(logger, log4cpp::Priority::EMERG, msg)
#endif

#endif // LOG4CPP_VARIADIC_MACROS_SUPPORTED

// Streaming macros
// Generic streaming logging
#define LOG4CPP_STREAM(logger, priority, streamSequence)                                                               \
    do {                                                                                                               \
        if ((logger).isPriorityEnabled(priority)) {                                                                    \
            (logger).getStream(priority) << streamSequence;                                                            \
        }                                                                                                              \
    } while (0)

#ifdef LOG4CPP_DISABLE_DEBUG_S
#define LOG4CPP_DEBUG_S(logger, streamSequence) ((void)0)
#else
#define LOG4CPP_DEBUG_S(logger, streamSequence) LOG4CPP_STREAM(logger, log4cpp::Priority::DEBUG, streamSequence)
#endif

#ifdef LOG4CPP_DISABLE_INFO_S
#define LOG4CPP_INFO_S(logger, streamSequence) ((void)0)
#else
#define LOG4CPP_INFO_S(logger, streamSequence) LOG4CPP_STREAM(logger, log4cpp::Priority::INFO, streamSequence)
#endif

#ifdef LOG4CPP_DISABLE_NOTICE_S
#define LOG4CPP_NOTICE_S(logger, streamSequence) ((void)0)
#else
#define LOG4CPP_NOTICE_S(logger, streamSequence) LOG4CPP_STREAM(logger, log4cpp::Priority::NOTICE, streamSequence)
#endif

#ifdef LOG4CPP_DISABLE_WARN_S
#define LOG4CPP_WARN_S(logger, streamSequence) ((void)0)
#else
#define LOG4CPP_WARN_S(logger, streamSequence) LOG4CPP_STREAM(logger, log4cpp::Priority::WARN, streamSequence)
#endif

#ifdef LOG4CPP_DISABLE_ERROR_S
#define LOG4CPP_ERROR_S(logger, streamSequence) ((void)0)
#else
#define LOG4CPP_ERROR_S(logger, streamSequence) LOG4CPP_STREAM(logger, log4cpp::Priority::ERROR, streamSequence)
#endif

#ifdef LOG4CPP_DISABLE_CRIT_S
#define LOG4CPP_CRIT_S(logger, streamSequence) ((void)0)
#else
#define LOG4CPP_CRIT_S(logger, streamSequence) LOG4CPP_STREAM(logger, log4cpp::Priority::CRIT, streamSequence)
#endif

#ifdef LOG4CPP_DISABLE_ALERT_S
#define LOG4CPP_ALERT_S(logger, streamSequence) ((void)0)
#else
#define LOG4CPP_ALERT_S(logger, streamSequence) LOG4CPP_STREAM(logger, log4cpp::Priority::ALERT, streamSequence)
#endif

#ifdef LOG4CPP_DISABLE_FATAL_S
#define LOG4CPP_FATAL_S(logger, streamSequence) ((void)0)
#else
#define LOG4CPP_FATAL_S(logger, streamSequence) LOG4CPP_STREAM(logger, log4cpp::Priority::FATAL, streamSequence)
#endif

#ifdef LOG4CPP_DISABLE_EMERG_S
#define LOG4CPP_EMERG_S(logger, streamSequence) ((void)0)
#else
#define LOG4CPP_EMERG_S(logger, streamSequence) LOG4CPP_STREAM(logger, log4cpp::Priority::EMERG, streamSequence)
#endif

#endif // LOG4CPP_LOGMACROS_H
