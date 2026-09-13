
set(CORE_SRCS
    src/Appender.cpp
    src/AppenderSkeleton.cpp
    src/AppendersFactory.cpp
    src/BufferingAppender.cpp
    src/FactoryParams.cpp
    src/LayoutsFactory.cpp
    src/LevelEvaluator.cpp
    src/Localtime.cpp
    src/PassThroughLayout.cpp
    src/TriggeringEventEvaluatorFactory.cpp
    src/LayoutAppender.cpp
    src/FileAppender.cpp
    src/RollingFileAppender.cpp
    src/FixedContextCategory.cpp
    src/IdsaAppender.cpp
    src/OstreamAppender.cpp
    src/StringQueueAppender.cpp
    src/SyslogAppender.cpp
    src/RemoteSyslogAppender.cpp
    src/SimpleLayout.cpp
    src/BasicLayout.cpp
    src/PatternLayout.cpp
    src/Category.cpp
    src/CategoryStream.cpp
    src/HierarchyMaintainer.cpp
    src/Configurator.cpp
    src/BasicConfigurator.cpp
    src/SimpleConfigurator.cpp
    src/PropertyConfigurator.cpp
    src/PropertyConfiguratorImpl.cpp
    src/LoggingEvent.cpp
    src/Priority.cpp
    src/NDC.cpp
    src/Filter.cpp
    src/TimeStamp.cpp
    src/StringUtil.cpp
    src/Properties.cpp
    src/Win32DebugAppender.cpp
    src/NTEventLogAppender.cpp
    src/DllMain.cpp
    src/DummyThreads.cpp
    src/MSThreads.cpp
    src/OmniThreads.cpp
    src/PThreads.cpp
    src/PortabilityImpl.cpp
    src/AbortAppender.cpp
    src/DailyRollingFileAppender.cpp)

foreach(SRC ${CORE_SRCS})
    list(APPEND LOG4CPP_SRCS ${LOG4CPP}/${SRC})
endforeach()

add_library(log4cpp SHARED ${LOG4CPP_SRCS})
target_compile_definitions(log4cpp PRIVATE
    HAVE_CONFIG_H
    LOG4CPP_HAVE_SNPRINTF
    LOG4CPP_HAVE_SSTREAM
    LOG4CPP_HAVE_UNISTD_H)
target_include_directories(log4cpp PRIVATE ${LOG4CPP}/include)
target_link_libraries(log4cpp PRIVATE Threads::Threads)
sidc_install_lib(log4cpp)
