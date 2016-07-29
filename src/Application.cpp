#include "Application.hpp"
#include <SDL_syswm.h>
#include <bgfx/bgfxplatform.h>
#include <bgfx/bgfx.h>
#include <cstdio>
extern "C" {
	#include <X11/Xlib.h>
}

namespace xveearr
{

class ApplicationImpl: public Application
{
public:
	bool init(const ApplicationContext& appCtx)
	{
		bgfx::sdlSetWindow(appCtx.mWindow);
		bgfx::init();
		bgfx::reset(1280, 720, BGFX_RESET_VSYNC);
		bgfx::setDebug(BGFX_DEBUG_TEXT | BGFX_DEBUG_STATS);
		bgfx::setViewClear(
			0,
			BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,
			0x303030ff,
			1.0f,
			0
		);

		mDisplay = XOpenDisplay(NULL);
		if(mDisplay == NULL) { return false; }

		return true;
	}

	bool onEvent(const SDL_Event& event)
	{
		return event.type != SDL_QUIT;
	}

	bool update()
	{
		return true;
	}

	void render()
	{
		bgfx::touch(0);
		bgfx::frame();
	}

	void shutdown()
	{
		XCloseDisplay(mDisplay);
		bgfx::shutdown();
	}

private:
	Display* mDisplay;
};

}

XVEEARR_DECLARE_APP(ApplicationImpl)
