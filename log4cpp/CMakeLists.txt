
include_directories(${LOG4CPP}/include)

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
    src/AbortAppender.cpp)

foreach(SRC ${CORE_SRCS})
    list(APPEND LOG4CPP_SRCS ${LOG4CPP}/${SRC})
endforeach()

add_definitions(-DHAVE_CONFIG_H -DLOG4CPP_HAVE_SSTREAM -DLOG4CPP_HAVE_UNISTD_H)

add_library(log4cpp SHARED ${LOG4CPP_SRCS})
sidc_install_lib(log4cpp)
