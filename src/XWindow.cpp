#include <iostream>
#include <unordered_set>
#include <bx/platform.h>
#include "DesktopEnvironment.hpp"
extern "C"
{
	#include <X11/Xlib.h>
	#include <X11/extensions/Xcomposite.h>
}
#define GLX_GLXEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>

namespace xveearr
{

namespace
{

struct WindowData
{
	GLuint mTexture;
};

}

class XWindow: public DesktopEnvironment
{
public:
	XWindow()
		:mDisplay(NULL)
	{}

	bool init(const ApplicationContext& appCtx)
	{
		(void)appCtx;
		if(XInitThreads() == 0) { return false; }

		mDisplay = XOpenDisplay(NULL);
		if(mDisplay == NULL) { return false; }

		XGrabServer(mDisplay);
		for(int i = 0; i < ScreenCount(mDisplay); ++i)
		{
			Window rootWindow = RootWindow(mDisplay, i);
			XCompositeRedirectSubwindows(
				mDisplay, rootWindow, CompositeRedirectAutomatic
			);
			XSelectInput(mDisplay, rootWindow, SubstructureNotifyMask);
		}
		XUngrabServer(mDisplay);

		XSync(mDisplay, false);

		//XSetErrorHandler(&XWindow::onXError);

		return true;
	}

	void shutdown()
	{
		if(mDisplay != NULL) { XCloseDisplay(mDisplay); }
	}

	void update()
	{
		int numEvents = XPending(mDisplay);
		for(int i = 0; i < numEvents; ++i)
		{
			XEvent ev;
			XNextEvent(mDisplay, &ev);

			switch(ev.type)
			{
				case MapNotify:
					onMapNotify(ev.xmap);
					break;
				case UnmapNotify:
					onUnmapNotify(ev.xunmap);
					break;
				case ReparentNotify:
					onReparentNotify(ev.xreparent);
					break;
			}
		}
	}

	void enumerateWindows(WindowEnumFn enumFn, void* context)
	{
		for(Window window: mWindows)
		{
			enumFn((uintptr_t)window, context);
		}
	}

	uintptr_t createTextureForWindow(WindowId wndId)
	{
		const int pixmapConfig[] = {
			GLX_BIND_TO_TEXTURE_RGBA_EXT, True,
			GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
			GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
			GLX_DOUBLEBUFFER, False,
			GLX_Y_INVERTED_EXT, (int)GLX_DONT_CARE,
			None
		};

		const int pixmapAttrs[] = {
			GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
			GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGB_EXT,
			None
		};

		int numConfigs = 0;
		GLXFBConfig* configs =
			glXChooseFBConfig(mDisplay, 0, pixmapConfig, &numConfigs);
		if(numConfigs == 0) { return 0; }
		printf("%d\n", numConfigs);

		Pixmap pixmap = XCompositeNameWindowPixmap(mDisplay, wndId);
		GLXPixmap glxpixmap =
			glXCreatePixmap(mDisplay, configs[0],  pixmap, pixmapAttrs);

		GLuint texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glXBindTexImageEXT(mDisplay, glxpixmap, GLX_FRONT_LEFT_EXT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		return texture;
	}

	void beginRender()
	{
	}

	void endRender()
	{
	}

	void getWindowSize(WindowId id, unsigned int& width, unsigned int& height)
	{
		Window root;
		int x, y;
		unsigned borderWidth, depth;
		XGetGeometry(
			mDisplay, id,
			&root,
			&x, &y,
			&width, &height,
			&borderWidth,
			&depth
		);
	}

private:
	void onMapNotify(const XMapEvent& ev)
	{
		mWindows.insert(ev.window);
	}

	void onUnmapNotify(const XUnmapEvent& ev)
	{
		mWindows.erase(ev.window);
	}

	void onReparentNotify(const XReparentEvent& ev)
	{
		mWindows.erase(ev.window);
	}

	void onXError(XErrorEvent* error)
	{
		(void)error;
	}

	static int onXError(Display* display, XErrorEvent* error)
	{
		(void)display;

		((XWindow&)getInstance()).onXError(error);
		return 0;
	}

	Display* mDisplay;
	std::unordered_set<Window> mWindows;
};

}

#if BX_PLATFORM_LINUX == 1
	XVEEARR_DECLARE_DE(XWindow)
#endif
