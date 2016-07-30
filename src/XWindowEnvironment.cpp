#include <bx/platform.h>
#include "DesktopEnvironment.hpp"
extern "C"
{
	#include <X11/Xlib.h>
}

namespace xveearr
{

class XWindowEnvironment: public DesktopEnvironment
{
public:
	XWindowEnvironment()
		:mDisplay(NULL)
	{}

	bool init(const ApplicationContext& appCtx)
	{
		if(XInitThreads() == 0) { return false; }

		mDisplay = XOpenDisplay(NULL);
		if(mDisplay == NULL) { return false; }

		(void)appCtx;
		return true;
	}

	void shutdown()
	{
		if(mDisplay != NULL) { XCloseDisplay(mDisplay); }
	}

	void update()
	{
	}

	void enumerateWindows(WindowEnumFn enumFn, void* context)
	{
		(void)enumFn;
		(void)context;
	}

	uintptr_t createTextureForWindow(WindowId wndId)
	{
		(void)wndId;
		return (uintptr_t)0;
	}

	void beginRender()
	{
	}

	void endRender()
	{
	}

private:
	Display* mDisplay;
};

}

#if BX_PLATFORM_LINUX == 1
	XVEEARR_DECLARE_DE(XWindowEnvironment)
#endif
