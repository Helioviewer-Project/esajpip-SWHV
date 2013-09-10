
PROJECT(log4cpp)

INCLUDE_DIRECTORIES(. ${LOG4CPP}/include)

SET(CORE_SRCS
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

FOREACH(SRC ${CORE_SRCS})
	SET(LOG4CPP_SRCS ${LOG4CPP_SRCS} ${LOG4CPP}/${SRC})
ENDFOREACH(SRC)

IF(WIN32)
	ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
ELSE(WIN32)
	IF(APPLE)
		ADD_DEFINITIONS(-DNDEBUG -DLOG4CPP_HAVE_SSTREAM)
	ELSE(APPLE)
		ADD_DEFINITIONS(-pthread -DNDEBUG -DLOG4CPP_HAVE_SSTREAM)
	ENDIF(APPLE)
ENDIF(WIN32)

ADD_LIBRARY(log4cpp ${LOG4CPP_SRCS})
