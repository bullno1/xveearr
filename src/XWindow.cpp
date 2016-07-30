#include <iostream>
#include <unordered_map>
#include <SDL_syswm.h>
#include <SDL.h>
#include <bx/platform.h>
#include <bx/spscqueue.h>
#include <bx/macros.h>
#include <bgfx/bgfxplatform.h>
#include <bgfx/bgfx.h>
#include "Application.hpp"
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

struct Texture
{
	bgfx::TextureHandle mBgfxHandle;
	GLuint mGLHandle;
};

struct WindowData
{
	Texture mTexture;
};

}

class XWindow: public DesktopEnvironment
{
public:
	XWindow()
		:mDisplay(NULL)
		,mHasXError(false)
		,mRendererContext(NULL)
		,mWorkerContext(NULL)
	{}

	bool init(const ApplicationContext& appCtx)
	{
		mWindow = appCtx.mWindow;

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

		return checkXStatus();
	}

	void shutdown()
	{
		if(mDisplay != NULL) { XCloseDisplay(mDisplay); }
	}

	void update()
	{
		if(BX_UNLIKELY(mRendererContext && !mWorkerContext))
		{
			SDL_SysWMinfo wminfo;
			SDL_VERSION(&wminfo.version);
			SDL_GetWindowWMInfo(mWindow, &wminfo);

			const int glxAttrs[] =
			{
				GLX_RENDER_TYPE, GLX_RGBA_BIT,
				GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
				GLX_DOUBLEBUFFER, true,
				GLX_RED_SIZE, 8,
				GLX_BLUE_SIZE, 8,
				GLX_GREEN_SIZE, 8,
				GLX_DEPTH_SIZE, 24,
				GLX_STENCIL_SIZE, 8,
				0,
			};

			int numConfigs;
			Display* display = wminfo.info.x11.display;
			XLockDisplay(display);
			GLXFBConfig* configs = glXChooseFBConfig(
				display, DefaultScreen(display), glxAttrs, &numConfigs
			);
			XVisualInfo* visualInfo = glXGetVisualFromFBConfig(display, configs[0]);
			mWorkerContext = glXCreateContext(
				display, visualInfo, mRendererContext, GL_TRUE
			);
			glXMakeCurrent(display, wminfo.info.x11.window, mWorkerContext);
			XFree(visualInfo);
			XFree(configs);
			XUnlockDisplay(wminfo.info.x11.display);
		}

		int numEvents = XPending(mDisplay);
		for(int i = 0; i < numEvents; ++i)
		{
			XEvent ev;
			XNextEvent(mDisplay, &ev);

			if(BX_UNLIKELY(!mWorkerContext)) { continue; }

			switch(ev.type)
			{
				case MapNotify:
					addWindow(ev.xmap.window);
					break;
				case UnmapNotify:
					removeWindow(ev.xunmap.window);
					break;
				case ReparentNotify:
					removeWindow(ev.xreparent.window);
					break;
			}
		}
	}

	void enumerateWindows(WindowEnumFn enumFn, void* context)
	{
		for(std::pair<Window, WindowData>&& pair: mWindows)
		{
			enumFn((uintptr_t)pair.first, context);
		}
	}

	bgfx::TextureHandle getTexture(WindowId wndId)
	{
		auto itr = mWindows.find(wndId);
		if(itr == mWindows.end())
		{
			bgfx::TextureHandle invalidHandle = BGFX_INVALID_HANDLE;
			return invalidHandle;
		}
		else
		{
			return itr->second.mTexture.mBgfxHandle;
		}
	}

	void beginRender()
	{
	}

	void endRender()
	{
		if(BX_UNLIKELY(mRendererContext == NULL))
		{
			mRendererContext = glXGetCurrentContext();
		}

		while(mRebindQueue.peek())
		{
			Texture* texture = mRebindQueue.pop();
			bgfx::overrideInternal(texture->mBgfxHandle, texture->mGLHandle);
			delete texture;
		}
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

		if(!checkXStatus())
		{
			width = 0;
			height = 0;
		}
	}

private:
	bool checkXStatus()
	{
		XSync(mDisplay, false);

		bool hasError = mHasXError;
		mHasXError = false;
		return !hasError;
	}

	void addWindow(Window window)
	{
		auto itr = mWindows.find(window);
		if(itr != mWindows.end()) { return; }

		WindowData data;
		data.mTexture.mBgfxHandle =
			bgfx::createTexture2D(1, 1, 0, bgfx::TextureFormat::RGBA8);
		glGenTextures(1, &data.mTexture.mGLHandle);
		glBindTexture(GL_TEXTURE_2D, data.mTexture.mGLHandle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		bindWindowToTexture(window, data.mTexture.mGLHandle);
		mWindows.insert(std::make_pair(window, data));

		mRebindQueue.push(new Texture(data.mTexture));
	}

	void removeWindow(Window window)
	{
		auto itr = mWindows.find(window);
		if(itr == mWindows.end()) { return; }

		bgfx::destroyTexture(itr->second.mTexture.mBgfxHandle);
		mWindows.erase(itr);
	}

	void bindWindowToTexture(Window window, GLuint texture)
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

		Pixmap pixmap = XCompositeNameWindowPixmap(mDisplay, window);

		GLXPixmap glxPixmap =
			glXCreatePixmap(mDisplay, configs[0],  pixmap, pixmapAttrs);
		glBindTexture(GL_TEXTURE_2D, texture);
		glXBindTexImageEXT(mDisplay, glxPixmap, GLX_FRONT_LEFT_EXT, NULL);
		glFinish();

		XFree(configs);
	}

	void onXError(const XErrorEvent& error)
	{
		(void)error;
		mHasXError = true;
	}

	static int onXError(Display* display, XErrorEvent* error)
	{
		(void)display;
		((XWindow&)DesktopEnvironment::getInstance()).onXError(*error);
		return 0;
	}

	Display* mDisplay;
	std::unordered_map<Window, WindowData> mWindows;
	SDL_Window* mWindow;
	bx::SpScUnboundedQueue<Texture> mRebindQueue;
	bool mHasXError;
	GLXContext mRendererContext;
	GLXContext mWorkerContext;
};

}

#if BX_PLATFORM_LINUX == 1
	XVEEARR_DECLARE_DE(XWindow)
#endif
