#include "IController.hpp"
#include <SDL.h>
#include <SDL_syswm.h>
#include <bx/platform.h>
#include <bx/fpumath.h>
#include "Registry.hpp"
#include "IWindowManager.hpp"
#include "IHMD.hpp"
#include "Log.hpp"

#if BX_PLATFORM_LINUX == 1
#	include <X11/Xlib-xcb.h>
#	include <xcb/xcb_keysyms.h>
#	include <xcb/xcb_util.h>
#	define XK_LATIN1
#	include <X11/keysymdef.h>
#endif

namespace xveearr
{

namespace
{

static const uint16_t gModMask = XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT;
static const xcb_keycode_t gKeyCode = XK_G;

}

class Keyboard: public IController
{
public:
	Keyboard()
		:mGrabbedGroup(0)
	{
#if BX_PLATFORM_LINUX == 1
		mXcbConn = NULL;
		mKeySyms = NULL;
#endif
	}

	bool init(const ControllerCfg& cfg)
	{
		mWindowManager = cfg.mWindowManager;
		mHMD = cfg.mHMD;

		SDL_SysWMinfo wmi;
		SDL_GetVersion(&wmi.version);
		SDL_GetWindowWMInfo(cfg.mWindow, &wmi);

#if BX_PLATFORM_LINUX == 1
		XVR_ENSURE(wmi.subsystem == SDL_SYSWM_X11, "Unsupported subsystem");

		mXcbConn = xcb_connect(NULL, NULL);
		XVR_ENSURE(mXcbConn, "Could not connect to X server");

		mKeySyms = xcb_key_symbols_alloc(mXcbConn);

		xcb_keycode_t* keycodes = xcb_key_symbols_get_keycode(mKeySyms, gKeyCode);
		for(
			xcb_screen_iterator_t itr =
				xcb_setup_roots_iterator(xcb_get_setup(mXcbConn));
			itr.rem;
			xcb_screen_next(&itr)
		)
		{
			xcb_window_t rootWindow = itr.data->root;

			for(
				xcb_keycode_t* itr = keycodes;
				*itr != XCB_NO_SYMBOL;
				++itr
			)
			{
				xcb_grab_key(
					mXcbConn,
					1,
					rootWindow,
					gModMask,
					*itr,
					XCB_GRAB_MODE_ASYNC,
					XCB_GRAB_MODE_ASYNC
				);
			}
		}
		xcb_flush(mXcbConn);

		free(keycodes);

		return true;
#else
		XVR_LOG(ERROR, "Unsupported platform");
		return false;
#endif
	}

	void shutdown()
	{
#if BX_PLATFORM_LINUX == 1
		if(mKeySyms) { xcb_key_symbols_free(mKeySyms); }
		if(mXcbConn) { xcb_disconnect(mXcbConn); }
#endif
	}

	const char* getName() const
	{
		return "keyboard";
	}

	void update()
	{
#if BX_PLATFORM_LINUX == 1
		xcb_generic_event_t* ev;
		while((ev = xcb_poll_for_event(mXcbConn)))
		{
			switch(XCB_EVENT_RESPONSE_TYPE(ev))
			{
				case XCB_KEY_PRESS:
					{
						xcb_keysym_t keysym = xcb_key_press_lookup_keysym(
							mKeySyms,
							(xcb_key_press_event_t*)ev,
							1
						);
						uint16_t state = ((xcb_key_press_event_t*)ev)->state;
						bool isKeyCombo = true
							&& keysym == gKeyCode
							&& (state & gModMask);
						if(isKeyCombo)
						{
							if(!mGrabbedGroup)
							{
								beginGrab();
							}
							else
							{
								endGrab();
							}
						}
					}
					break;
			}

			free(ev);
		}
#endif

		if(!mGrabbedGroup) { return; }

		float headTransform[16];
		mHMD->getHeadTransform(headTransform);
		float groupTransform[16];
		bx::mtxMul(groupTransform, mGrabTransform, headTransform);
		if(!mWindowManager->setGroupTransform(mGrabbedGroup, groupTransform))
		{
			endGrab();
		}
	}

private:
	void beginGrab()
	{
		XVR_LOG(DEBUG, "Begin grabbing window");

		WindowId focusedWindow = mWindowManager->getFocusedWindow();
		if(!focusedWindow) { return; }

		const WindowInfo* wndInfo = mWindowManager->getWindowInfo(focusedWindow);
		if(!wndInfo) { return; }

		mGrabbedGroup = wndInfo->mPID;

		float groupTransform[16];
		mWindowManager->getGroupTransform(wndInfo->mPID, groupTransform);
		float headTransform[16];
		mHMD->getHeadTransform(headTransform);
		float invHeadTransform[16];
		bx::mtxInverse(invHeadTransform, headTransform);
		bx::mtxMul(mGrabTransform, groupTransform, invHeadTransform);
	}

	void endGrab()
	{
		mGrabbedGroup = 0;
		XVR_LOG(DEBUG, "End grabbing window");
	}

	IWindowManager* mWindowManager;
	IHMD* mHMD;
	PID mGrabbedGroup;
	float mGrabTransform[16];
#if BX_PLATFORM_LINUX == 1
	xcb_connection_t* mXcbConn;
	xcb_key_symbols_t* mKeySyms;
#endif
};

#if BX_PLATFORM_LINUX == 1

XVR_REGISTER(IController, Keyboard)

#endif

}
