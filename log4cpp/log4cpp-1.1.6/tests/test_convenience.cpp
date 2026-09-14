#include "log4cpp/OstreamAppender.hh"
#include <iostream>

#include <log4cpp/Category.hh>

// Set active log level to strip all logs below INFO at compile-time
#define LOG4CPP_ACTIVE_LEVEL LOG4CPP_PRIORITY_INFO
#include <log4cpp/LogMacros.hh>

log4cpp::Category& logger = log4cpp::Category::getRoot();

int main() {
    // Initialize the root logger
    logger.setPriority(log4cpp::Priority::DEBUG);

    log4cpp::OstreamAppender ostreamAppender("cout", &std::cout);
    ostreamAppender.setLayout(new log4cpp::BasicLayout());

    logger.removeAllAppenders();
    logger.addAppender(ostreamAppender);

#if LOG4CPP_VARIADIC_MACROS_SUPPORTED
    // message with arguments
    int value = 42;
    int diskPercent = 12;
    int memoryMb = 128;
    int temperature = 30;
    std::string filename = "config.txt";
    std::string moduleName = "CoreModule";
    std::string alertMsg = "High temperature detected";

    LOG4CPP_DEBUG(logger, "Debug value: %d", value);
    std::cout
        << "Debug value message should have not been printed out, since it should have been removed at compile-time"
        << std::endl;
    LOG4CPP_INFO(logger, "Application started");
    LOG4CPP_NOTICE(logger, "Configuration loaded successfully");
    LOG4CPP_WARN(logger, "Low disk space: %d%% remaining", diskPercent);
    LOG4CPP_WARN(logger, "Low memory: %d MB remaining", memoryMb);
    LOG4CPP_ERROR(logger, "Failed to open file: %s", filename.c_str());
    LOG4CPP_CRIT(logger, "Critical error in module %s", moduleName.c_str());
    LOG4CPP_ALERT(logger, "Immediate attention required: %s; %d higher than average", alertMsg.c_str(), temperature);
    LOG4CPP_FATAL(logger, "Fatal error, shutting down");
    LOG4CPP_EMERG(logger, "System is unusable!");
#else
    // message without arguments
    LOG4CPP_EMERG(logger, "emerg level, no variadic support");
    LOG4CPP_FATAL(logger, "fatal level, no variadic support");
    LOG4CPP_ALERT(logger, "alert level, no variadic support");
    LOG4CPP_CRIT(logger, "crit level, no variadic support");
    LOG4CPP_ERROR(logger, "error level, no variadic support");
    LOG4CPP_WARN(logger, "warn level, no variadic support");
    LOG4CPP_NOTICE(logger, "notice level, no variadic support");
    LOG4CPP_INFO(logger, "info level, no variadic support");
    LOG4CPP_DEBUG(logger, "debug level, no variadic support");
    std::cout << "debug level should have not been printed out, since it should have been removed at compile-time"
              << std::endl;
#endif

    LOG4CPP_EMERG_S(logger, "emerg level via stream macro");
    LOG4CPP_FATAL_S(logger, "fatal level via stream " << "macro");
    LOG4CPP_ALERT_S(logger, "alert level via " << "stream " << "macro");
    LOG4CPP_CRIT_S(logger, "crit level " << "via " << "stream " << "macro");
    LOG4CPP_ERROR_S(logger, "error " << "level " << "via " << "stream " << "macro");
    LOG4CPP_WARN_S(logger, "warn level via stream macro" << ' ' << 3.14f);
    LOG4CPP_NOTICE_S(logger, "notice level via stream macro" << " " << 42);
    LOG4CPP_INFO_S(logger, "info level via stream macro" << "");
    LOG4CPP_DEBUG_S(logger, "debug level via stream macro");
    std::cout
        << "Debug stream message should have not been printed out, since it should have been removed at compile-time"
        << std::endl;

    return 0;
}