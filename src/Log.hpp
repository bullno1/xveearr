#ifndef XVEEARR_LOG_HPP
#define XVEEARR_LOG_HPP

#include <iosfwd>
#include <bx/macros.h>

#define XVR_LOG(LEVEL, ...) \
	::xveearr::Log(::xveearr::Log::LEVEL, __FILE__, __LINE__), __VA_ARGS__

#define XVR_ENSURE(EXP, ...) \
	do { \
		if(!(EXP)) { \
			XVR_LOG(Error, __VA_ARGS__); \
			return false; \
		} \
	} while(0)

namespace xveearr
{

class Log
{
public:
	enum Level
	{
		Trace,
		Debug,
		Info,
		Warn,
		Error,

		Count
	};

	Log(Level level, const char* file, unsigned int line);
	~Log();

	template<typename T>
	Log& operator,(const T& msg);

	template<typename T>
	Log& operator<<(const T& msg);

	static void setLogLevel(Level level);
	static void setOutputStream(std::ostream& stream);
	static Level parseLogLevel(const char* level);
private:
	Level mLevel;
	const char* mFile;
	unsigned int mLine;
	bool mOpened;

	static Level sLogLevel;
	static std::ostream* sOutputStream;
};

}

#include "Log.inl"

#endif
