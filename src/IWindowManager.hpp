#ifndef XVEEARR_WINDOW_MANAGER_HPP
#define XVEEARR_WINDOW_MANAGER_HPP

#include "IWindowSystem.hpp"

namespace xveearr
{

class IWindowManager
{
public:
	virtual unsigned int getWindowGroups(const PID** pids) = 0;
	virtual unsigned int getWindows(PID group, const WindowId** wids) = 0;
	virtual void getGroupTransform(PID pid, float* result) = 0;
	virtual void setGroupTransform(PID pid, const float* mtx) = 0;
	virtual void transformPoint(
		PID pid, unsigned int x, unsigned int y, float* out
	) = 0;
	virtual void focusWindow(WindowId window) = 0;
	virtual const WindowInfo* getWindowInfo(WindowId window) = 0;
};

}

#endif
