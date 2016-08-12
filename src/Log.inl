#include "Log.hpp"
#include <sstream>

namespace xveearr
{

inline Log::Log(const char* level, const char* file, unsigned int line)
	:mLevel(level)
	,mFile(file)
	,mLine(line)
{
	std::stringstream ss;
	ss << mFile << ":" << mLine;
	std::cout << std::left
		<< "[" << std::setw(5) << mLevel << std::setw(0) << "] @ "
		<< std::setw(25) << ss.str() << std::setw(0) << std::internal
		<< ": ";
}

inline Log::~Log()
{
	std::cout << std::endl;
}

template<typename T>
inline Log& Log::operator,(const T& msg)
{
	return (*this) << msg;
}

template<typename T>
inline Log& Log::operator<<(const T& msg)
{
	std::cout << msg;
	return *this;
}

}
