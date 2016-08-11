#ifndef XVEEARR_LOG_HPP
#define XVEEARR_LOG_HPP

#include <iostream>
#include <iomanip>

#define XVR_LOG(LEVEL, ...) \
	::xveearr::Log(#LEVEL, __FILE__, __LINE__), __VA_ARGS__

#define XVR_ENSURE(EXP, ...) \
	do { \
		if(!(EXP)) { \
			XVR_LOG(ERROR, __VA_ARGS__); \
			return false; \
		} \
	} while(0)

namespace xveearr
{

class Log
{
public:
	Log(const char* level, const char* file, unsigned int line);
	~Log();

	template<typename T>
	Log& operator,(const T& msg);

	template<typename T>
	Log& operator<<(const T& msg);
private:
	const char* mLevel;
	const char* mFile;
	unsigned int mLine;
};

}

#include "Log.inl"

#endif
