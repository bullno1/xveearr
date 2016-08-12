#include "Log.hpp"
#include <iostream>
#include <iomanip>
#include <bx/string.h>

namespace xveearr
{

namespace
{

static const char* gLevelLabels[] = {
	"TRACE",
	"DEBUG",
	"INFO",
	"WARN",
	"ERROR"
};

}

Log::Level Log::sLogLevel = Log::Info;
std::ostream* Log::sOutputStream = &std::cout;

Log::Log(Log::Level level, const char* file, unsigned int line)
	:mLevel(level)
	,mFile(file)
	,mLine(line)
	,mOpened(level >= sLogLevel)
{
	if(mOpened)
	{
		std::stringstream ss;
		ss << mFile << ":" << mLine;
		std::cout << std::left
			<< "[" << std::setw(5) << gLevelLabels[mLevel] << std::setw(0) << "] @ "
			<< std::setw(25) << ss.str() << std::setw(0) << std::internal
			<< ": ";
	}
}

Log::~Log()
{
	if(mOpened) { std::cout << std::endl; }
}

void Log::setLogLevel(Log::Level level)
{
	sLogLevel = level;
}

void Log::setOutputStream(std::ostream& stream)
{
	sOutputStream = &stream;
}

Log::Level Log::parseLogLevel(const char* level)
{
	if(bx::stricmp(level, "trace") == 0)
	{
		return Log::Trace;
	}
	else if(bx::stricmp(level, "debug") == 0)
	{
		return Log::Debug;
	}
	else if(bx::stricmp(level, "info") == 0)
	{
		return Log::Info;
	}
	else if(bx::stricmp(level, "warn") == 0)
	{
		return Log::Warn;
	}
	else if(bx::stricmp(level, "error") == 0)
	{
		return Log::Error;
	}
	else
	{
		return Log::Count;
	}
}

}
