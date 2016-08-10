#ifndef XVEEARR_UTILS_HPP
#define XVEEARR_UTILS_HPP

#include "IWindowManager.hpp"

namespace xveearr
{
namespace utils
{

WindowId pickWindow(
	IWindowManager* windowManager,
	float* rayOrigin,
	float* rayDirection
);

}
}

#endif
