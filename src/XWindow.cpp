#include "DesktopEnvironment.hpp"
#include <unordered_map>
#include <vector>
#include <SDL_syswm.h>
#include <SDL.h>
#include <bx/platform.h>
#include <bx/spscqueue.h>
#include <bx/macros.h>
#include <bgfx/bgfxplatform.h>
#include <bgfx/bgfx.h>
#include <X11/Xlib-xcb.h>
#include <xcb/composite.h>
#include <xcb/glx.h>
#define GLX_GLXEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>

namespace xveearr
{

namespace
{

struct RebindReq
{
	bgfx::TextureHandle mBgfxHandle;
	GLuint mGLHandle;
};

struct WindowData: WindowInfo
{
	GLuint mGLTexture;
	xcb_pixmap_t mCompositePixmap;
	xcb_pixmap_t mGLXPixmap;
};

}

class XWindow: public DesktopEnvironment
{
public:
	XWindow()
		:mDisplay(NULL)
		,mXcbConn(NULL)
		,mRendererContext(NULL)
		,mWorkerContext(NULL)
	{}

	bool init(const ApplicationContext& appCtx)
	{
		mWindow = appCtx.mWindow;

		mDisplay = XOpenDisplay(NULL);
		if(mDisplay == NULL) { return false; }

		mXcbConn = XGetXCBConnection(mDisplay);
		XSetEventQueueOwner(mDisplay, XCBOwnsEventQueue);

		xcb_generic_error_t *error;
		xcb_void_cookie_t voidCookie;

		voidCookie = xcb_grab_server_checked(mXcbConn);
		if((error = xcb_request_check(mXcbConn, voidCookie)))
		{
			free(error);
			return false;
		}

		int numScreens = xcb_setup_roots_length(xcb_get_setup(mXcbConn));
		std::vector<xcb_void_cookie_t> cookies;
		cookies.reserve(numScreens * 2);

		xcb_screen_iterator_t itr;
		for(
			itr = xcb_setup_roots_iterator(xcb_get_setup(mXcbConn));
			itr.rem;
			xcb_screen_next(&itr)
		)
		{
			xcb_window_t rootWindow = itr.data->root;

			cookies.push_back(
				xcb_composite_redirect_subwindows_checked(
					mXcbConn, rootWindow, XCB_COMPOSITE_REDIRECT_AUTOMATIC
				)
			);

			const uint32_t eventMask[] = {
				XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, 0
			};
			cookies.push_back(
				xcb_change_window_attributes_checked(
					mXcbConn, rootWindow, XCB_CW_EVENT_MASK, eventMask
				)
			);
		}

		voidCookie = xcb_ungrab_server_checked(mXcbConn);
		if((error = xcb_request_check(mXcbConn, voidCookie)))
		{
			free(error);
			return false;
		}

		for(xcb_void_cookie_t cookie: cookies)
		{
			if((error = xcb_request_check(mXcbConn, cookie)))
			{
				free(error);
				return false;
			}
		}

		return true;
	}

	void shutdown()
	{
		if(mDisplay != NULL) { XCloseDisplay(mDisplay); }
	}

	bool pollEvent(WindowEvent& event)
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
			XVisualInfo* visualInfo =
				glXGetVisualFromFBConfig(display, configs[0]);
			mWorkerContext = glXCreateContext(
				display, visualInfo, mRendererContext, GL_TRUE
			);
			glXMakeCurrent(display, wminfo.info.x11.window, mWorkerContext);
			XFree(visualInfo);
			XFree(configs);
			XUnlockDisplay(wminfo.info.x11.display);
		}

		if(BX_UNLIKELY(!mWorkerContext))
		{
			xcb_generic_event_t* ev;
			while((ev = xcb_poll_for_event(mXcbConn))) { free(ev); }

			return false;
		}

		xcb_generic_event_t* ev = xcb_poll_for_event(mXcbConn);
		if(!ev) { return false; }

		bool result;
		switch(ev->response_type & ~0x80)
		{
			case XCB_MAP_NOTIFY:
				result = onWindowAdded(
					((xcb_map_notify_event_t*)ev)->window, event
				);
				break;
			case XCB_UNMAP_NOTIFY:
				result = onWindowRemoved(
					((xcb_unmap_notify_event_t*)ev)->window, event
				);
				break;
			case XCB_REPARENT_NOTIFY:
				result = onWindowRemoved(
					((xcb_reparent_notify_event_t*)ev)->window, event
				);
				break;
			default:
				result = false;
				break;
		}

		free(ev);
		return result;
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
			RebindReq* req = mRebindQueue.pop();
			bgfx::overrideInternal(req->mBgfxHandle, req->mGLHandle);
			delete req;
		}
	}

	const WindowInfo* getWindowInfo(WindowId id)
	{
		auto itr = mWindows.find(id);
		return itr != mWindows.end() ? &itr->second : NULL;
	}

private:
	bool onWindowAdded(xcb_window_t window, WindowEvent& event)
	{
		auto itr = mWindows.find(window);
		if(itr != mWindows.end()) { return false; }

		xcb_pixmap_t pixmap;
		GLXFBConfig fbConfig;
		xcb_get_geometry_reply_t geom;
		if(!getWindowInfo(window, geom, pixmap, fbConfig))
		{
			return false;
		}

		const int pixmapAttrs[] = {
			GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
			GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGBA_EXT,
			None
		};
		GLXPixmap glxPixmap =
			glXCreatePixmap(mDisplay, fbConfig, pixmap, pixmapAttrs);

		GLuint glTexture;
		glGenTextures(1, &glTexture);
		glBindTexture(GL_TEXTURE_2D, glTexture);
		glXBindTexImageEXT(mDisplay, glxPixmap, GLX_FRONT_LEFT_EXT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glFinish();

		bgfx::TextureHandle bgfxTexture =
			bgfx::createTexture2D(1, 1, 0, bgfx::TextureFormat::RGBA8);

		WindowData data;
		data.mGLTexture = glTexture;
		data.mCompositePixmap = pixmap;
		data.mGLXPixmap = glxPixmap;
		data.mTexture = bgfxTexture;
		data.mX = geom.x;
		data.mY = geom.y;
		data.mWidth = geom.width;
		data.mHeight = geom.height;
		mWindows.insert(std::make_pair(window, data));

		RebindReq* req = new RebindReq;
		req->mGLHandle = glTexture;
		req->mBgfxHandle = bgfxTexture;
		mRebindQueue.push(req);

		event.mType = WindowEvent::WindowAdded;
		event.mId = window;
		event.mInfo = data;
		return true;
	}

	bool onWindowRemoved(xcb_window_t window, WindowEvent& event)
	{
		auto itr = mWindows.find(window);
		if(itr == mWindows.end()) { return false; }

		bgfx::destroyTexture(itr->second.mTexture);
		mWindows.erase(itr);

		event.mType = WindowEvent::WindowRemoved;
		event.mId = window;
		return true;
	}

	bool getWindowInfo(
		xcb_window_t window,
		xcb_get_geometry_reply_t& geom,
		xcb_pixmap_t& pixmap,
		GLXFBConfig& fbConfig
	)
	{
		pixmap = xcb_generate_id(mXcbConn);
		xcb_get_geometry_cookie_t getGeomCookie =
			xcb_get_geometry(mXcbConn, window);
		xcb_void_cookie_t namePixmapCookie =
			xcb_composite_name_window_pixmap_checked(mXcbConn, window, pixmap);
		xcb_get_geometry_reply_t* geomReply =
			xcb_get_geometry_reply(mXcbConn, getGeomCookie, NULL);
		xcb_generic_error_t* namePixmapError =
			xcb_request_check(mXcbConn, namePixmapCookie);

		if(geomReply == NULL || namePixmapError != NULL)
		{
			if(geomReply != NULL) { free(geomReply); }
			if(namePixmapError != NULL) { free(namePixmapError); }

			return false;
		}

		geom = *geomReply;
		free(geomReply);

		if(geom.depth == 0)
		{
			xcb_free_pixmap(mXcbConn, pixmap);
			return false;
		}

		const int pixmapConfig[] = {
			GLX_BIND_TO_TEXTURE_RGBA_EXT, True,
			GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
			GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
			GLX_DOUBLEBUFFER, False,
			GLX_Y_INVERTED_EXT, (int)GLX_DONT_CARE,
			None
		};

		int numConfigs = 0;
		GLXFBConfig* fbConfigs =
			glXChooseFBConfig(mDisplay, 0, pixmapConfig, &numConfigs);

		for(int i = 0; i < numConfigs; ++i)
		{
			XVisualInfo* visualInfo =
				glXGetVisualFromFBConfig(mDisplay, fbConfigs[i]);

			if(visualInfo->depth == geom.depth)
			{
				fbConfig = fbConfigs[i];
				XFree(visualInfo);
				XFree(fbConfigs);
				return true;
			}

			XFree(visualInfo);
		}

		XFree(fbConfigs);
		return false;
	}

	Display* mDisplay;
	xcb_connection_t* mXcbConn;
	SDL_Window* mWindow;
	GLXContext mRendererContext;
	GLXContext mWorkerContext;
	std::unordered_map<Window, WindowData> mWindows;
	bx::SpScUnboundedQueue<RebindReq> mRebindQueue;
};

#if BX_PLATFORM_LINUX == 1

static XWindow gXWindowInstance;

DesktopEnvironment* DesktopEnvironment::getInstance()
{
	return &gXWindowInstance;
}

#endif

}
