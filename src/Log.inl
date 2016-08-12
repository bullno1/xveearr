#include "Log.hpp"
#include <sstream>

namespace xveearr
{

template<typename T>
inline Log& Log::operator,(const T& msg)
{
	return (*this) << msg;
}

template<typename T>
inline Log& Log::operator<<(const T& msg)
{
	if(mOpened) { *sOutputStream << msg; }
	return *this;
}

}
