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
#include <xcb/xcb_util.h>
#define GLX_GLXEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>

namespace xveearr
{

namespace
{

struct TextureReq
{
	enum Type
	{
		Bind,
		Rebind,
		Unbind,

		Count
	};

	Type mType;
	bgfx::TextureHandle mBgfxHandle;
	xcb_window_t mWindow;
	int mFBConfigId;
};

struct TextureInfo
{
	GLuint mGLHandle;
	xcb_window_t mWindow;
	xcb_pixmap_t mCompositePixmap;
	xcb_glx_pixmap_t mGLXPixmap;
};

const uint32_t GLX_PIXMAP_ATTRS[] = {
	GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
	GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGBA_EXT,
	None
};

}

class XWindow: public DesktopEnvironment
{
public:
	XWindow()
		:mDisplay(NULL)
		,mXcbConn(NULL)
		,mEventIndex(0)
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
		cookies.reserve(numScreens);

		for(
			xcb_screen_iterator_t itr =
				xcb_setup_roots_iterator(xcb_get_setup(mXcbConn));
			itr.rem;
			xcb_screen_next(&itr)
		)
		{
			xcb_window_t rootWindow = itr.data->root;

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

	bool pollEvent(WindowEvent& xvrEvent)
	{
		if(tryDrainEvent(xvrEvent)) { return true; }

		// Quickly drain all pending events into a temp buff
		mTmpEventBuff.clear();
		xcb_generic_event_t* xcbEvent;
		while((xcbEvent = xcb_poll_for_event(mXcbConn)))
		{
			WindowEvent tmpEvent;
			switch(XCB_EVENT_RESPONSE_TYPE(xcbEvent))
			{
				case XCB_MAP_NOTIFY:
					tmpEvent.mWindow =
						((xcb_map_notify_event_t*)xcbEvent)->window;
					tmpEvent.mType = WindowEvent::WindowAdded;
					bufferEvent(tmpEvent);
					break;
				case XCB_UNMAP_NOTIFY:
					tmpEvent.mWindow =
						((xcb_unmap_notify_event_t*)xcbEvent)->window;
					tmpEvent.mType = WindowEvent::WindowRemoved;
					bufferEvent(tmpEvent);
					break;
				case XCB_REPARENT_NOTIFY:
					// TODO: handle reparent to root
					tmpEvent.mWindow =
						((xcb_reparent_notify_event_t*)xcbEvent)->window;
					tmpEvent.mType = WindowEvent::WindowRemoved;
					bufferEvent(tmpEvent);
					break;
				case XCB_CONFIGURE_NOTIFY:
					{
						xcb_configure_notify_event_t* cfgNotifyEvent =
							(xcb_configure_notify_event_t*)xcbEvent;
						tmpEvent.mWindow = cfgNotifyEvent->window;
						tmpEvent.mType = WindowEvent::WindowUpdated;
						tmpEvent.mInfo.mX = cfgNotifyEvent->x;
						tmpEvent.mInfo.mY = cfgNotifyEvent->y;
						tmpEvent.mInfo.mWidth = cfgNotifyEvent->width;
						tmpEvent.mInfo.mHeight = cfgNotifyEvent->height;
						bufferEvent(tmpEvent);
					}
					break;
			}

			free(xcbEvent);
		}

		// Process buffered events and queue them
		mEventIndex = 0;
		mEvents.clear();
		for(WindowEvent tmpEvent: mTmpEventBuff)
		{
			bool accepted = false;
			switch(tmpEvent.mType)
			{
				case WindowEvent::WindowAdded:
					accepted = translateWindowAdded(tmpEvent);
					break;
				case WindowEvent::WindowRemoved:
					accepted = translateWindowRemoved(tmpEvent);
					break;
				case WindowEvent::WindowUpdated:
					accepted = translateWindowUpdated(tmpEvent);
					break;
			}

			if(accepted) { mEvents.push_back(tmpEvent); }
		}

		// Try draining again
		return tryDrainEvent(xvrEvent);
	}

	void initRenderer()
	{
		SDL_SysWMinfo wmi;
		SDL_GetVersion(&wmi.version);
		SDL_GetWindowWMInfo(mWindow, &wmi);

		mRendererDisplay = wmi.info.x11.display;
		mRendererXcbConn = XGetXCBConnection(mRendererDisplay);

		xcb_grab_server(mRendererXcbConn);
		for(
			xcb_screen_iterator_t itr =
				xcb_setup_roots_iterator(xcb_get_setup(mRendererXcbConn));
			itr.rem;
			xcb_screen_next(&itr)
		)
		{
			xcb_window_t rootWindow = itr.data->root;
			xcb_composite_redirect_subwindows(
				mRendererXcbConn, rootWindow, XCB_COMPOSITE_REDIRECT_AUTOMATIC
			);
		}
		xcb_ungrab_server(mRendererXcbConn);
	}

	void shutdownRenderer()
	{
	}

	void beginRender()
	{
		while(mTextureReqs.peek())
		{
			TextureReq* req = mTextureReqs.pop();
			if(req->mType == TextureReq::Bind)
			{
				mDeferredTextureReqs.push_back(*req);
			}
			else
			{
				executeTextureReq(*req);
			}
			delete req;
		}

		for(auto&& pair: mTextures)
		{
			glBindTexture(GL_TEXTURE_2D, pair.second.mGLHandle);
			glXBindTexImageEXT(
				mRendererDisplay,
				pair.second.mGLXPixmap,
				GLX_FRONT_LEFT_EXT,
				NULL
			);
		}
	}

	void endRender()
	{
		for(auto&& pair: mTextures)
		{
			glBindTexture(GL_TEXTURE_2D, pair.second.mGLHandle);
			glXReleaseTexImageEXT(
				mRendererDisplay,
				pair.second.mGLXPixmap,
				GLX_FRONT_LEFT_EXT
			);
		}

		for(const TextureReq& req: mDeferredTextureReqs)
		{
			executeTextureReq(req);
		}
		mDeferredTextureReqs.clear();
	}

	const WindowInfo* getWindowInfo(WindowId id)
	{
		auto itr = mWindows.find(id);
		return itr != mWindows.end() ? &itr->second : NULL;
	}

private:
	bool tryDrainEvent(WindowEvent& xvrEvent)
	{
		if(mEventIndex < mEvents.size())
		{
			xvrEvent = mEvents[mEventIndex++];
			return true;
		}
		else
		{
			return false;
		}
	}

	void bufferEvent(WindowEvent& event)
	{
		switch(event.mType)
		{
			case WindowEvent::WindowRemoved:
				{
					bool completeCycle = false;
					for(
						auto itr = mTmpEventBuff.begin();
						itr != mTmpEventBuff.end();
					)
					{
						if(itr->mWindow == event.mWindow)
						{
							itr = mTmpEventBuff.erase(itr);
							if(itr->mType == WindowEvent::WindowAdded)
							{
								completeCycle = true;
							}
						}
						else
						{
							++itr;
						}
					}

					if(!completeCycle) { mTmpEventBuff.push_back(event); }
				}
				break;
			case WindowEvent::WindowUpdated:
				{
					bool coalesced = false;
					for(WindowEvent& pastEvent: mTmpEventBuff)
					{
						if(pastEvent.mWindow == event.mWindow)
						{
							pastEvent.mInfo = event.mInfo;
							coalesced = true;
						}
					}

					if(!coalesced) { mTmpEventBuff.push_back(event); }
				}
				break;
			default:
				mTmpEventBuff.push_back(event);
				break;
		}
	}

	bool translateWindowAdded(WindowEvent& event)
	{
		auto itr = mWindows.find(event.mWindow);
		if(itr != mWindows.end()) { return false; }

		int fbConfigId;
		WindowInfo wndInfo;
		if(!getWindowInfo(event.mWindow, wndInfo, fbConfigId))
		{
			return false;
		}

		bgfx::TextureHandle texture =
			bgfx::createTexture2D(1, 1, 0, bgfx::TextureFormat::RGBA8);

		wndInfo.mTexture = texture;
		mWindows.insert(std::make_pair(event.mWindow, wndInfo));

		TextureReq* req = new TextureReq;
		req->mType = TextureReq::Bind;
		req->mBgfxHandle = texture;
		req->mWindow = event.mWindow;
		req->mFBConfigId = fbConfigId;
		mTextureReqs.push(req);

		event.mInfo = wndInfo;
		return true;
	}

	bool translateWindowRemoved(WindowEvent& event)
	{
		auto itr = mWindows.find(event.mWindow);
		if(itr == mWindows.end()) { return false; }

		TextureReq* req = new TextureReq;
		req->mType = TextureReq::Unbind;
		req->mBgfxHandle = itr->second.mTexture;
		mTextureReqs.push(req);

		bgfx::destroyTexture(itr->second.mTexture);
		mWindows.erase(itr);

		return true;
	}

	bool translateWindowUpdated(WindowEvent& event)
	{
		auto itr = mWindows.find(event.mWindow);
		if(itr == mWindows.end()) { return false; }

		WindowInfo& wndInfo = itr->second;

		unsigned int oldWidth = wndInfo.mWidth;
		unsigned int oldHeight = wndInfo.mHeight;
		wndInfo.mX = event.mInfo.mX;
		wndInfo.mY = event.mInfo.mY;
		wndInfo.mWidth = event.mInfo.mWidth;
		wndInfo.mHeight = event.mInfo.mHeight;

		if(oldWidth == event.mInfo.mWidth && oldHeight == event.mInfo.mHeight)
		{
			event.mInfo = wndInfo;
			return true;
		}

		int fbConfigId;
		if(!getWindowInfo(event.mWindow, wndInfo, fbConfigId))
		{
			return false;
		}

		TextureReq* req = new TextureReq;
		req->mType = TextureReq::Rebind;
		req->mBgfxHandle = wndInfo.mTexture;
		req->mFBConfigId = fbConfigId;
		mTextureReqs.push(req);

		event.mInfo = wndInfo;
		return true;
	}

	bool getWindowInfo(
		xcb_window_t window,
		WindowInfo& wndInfo,
		int& fbConfigId
	)
	{
		xcb_get_geometry_reply_t* geomReply = xcb_get_geometry_reply(
			mXcbConn, xcb_get_geometry(mXcbConn, window), NULL
		);

		if(geomReply == NULL) { return false; }

		xcb_get_geometry_reply_t geom = *geomReply;
		free(geomReply);

		if(geom.depth == 0) { return false; }

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
				wndInfo.mX = geom.x;
				wndInfo.mY = geom.y;
				wndInfo.mWidth = geom.width;
				wndInfo.mHeight = geom.height;
				glXGetFBConfigAttrib(
					mDisplay, fbConfigs[i], GLX_FBCONFIG_ID, &fbConfigId
				);
				XFree(visualInfo);
				XFree(fbConfigs);

				return true;
			}

			XFree(visualInfo);
		}

		XFree(fbConfigs);

		return false;
	}

	void executeTextureReq(const TextureReq& req)
	{
		switch(req.mType)
		{
			case TextureReq::Bind:
				bindTexture(req);
				break;
			case TextureReq::Unbind:
				unbindTexture(req);
				break;
			case TextureReq::Rebind:
				rebindTexture(req);
				break;
		}
	}

	void bindTexture(const TextureReq& req)
	{
		xcb_pixmap_t compositePixmap = xcb_generate_id(mRendererXcbConn);
		xcb_composite_name_window_pixmap(
			mRendererXcbConn, req.mWindow, compositePixmap
		);

		xcb_glx_pixmap_t glxPixmap = xcb_generate_id(mRendererXcbConn);
		xcb_glx_create_pixmap(
			mRendererXcbConn, 0, req.mFBConfigId, compositePixmap, glxPixmap,
			BX_COUNTOF(GLX_PIXMAP_ATTRS) / 2,
			GLX_PIXMAP_ATTRS
		);

		GLuint glTexture;
		glGenTextures(1, &glTexture);
		glBindTexture(GL_TEXTURE_2D, glTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		TextureInfo texInfo;
		texInfo.mWindow = req.mWindow;
		texInfo.mGLHandle = glTexture;
		texInfo.mCompositePixmap = compositePixmap;
		texInfo.mGLXPixmap = glxPixmap;
		mTextures.insert(std::make_pair(req.mBgfxHandle.idx, texInfo));

		bgfx::overrideInternal(req.mBgfxHandle, glTexture);
	}

	void unbindTexture(const TextureReq& req)
	{
		auto itr = mTextures.find(req.mBgfxHandle.idx);
		if(itr == mTextures.end()) { return; }

		TextureInfo& texInfo = itr->second;
		xcb_glx_destroy_pixmap(mRendererXcbConn, texInfo.mGLXPixmap);
		xcb_free_pixmap(mRendererXcbConn, texInfo.mCompositePixmap);
		glDeleteTextures(1, &texInfo.mGLHandle);
		mTextures.erase(itr);
	}

	void rebindTexture(const TextureReq& req)
	{
		auto itr = mTextures.find(req.mBgfxHandle.idx);
		if(itr == mTextures.end()) { return; }

		TextureInfo& texInfo = itr->second;
		xcb_glx_destroy_pixmap(mRendererXcbConn, texInfo.mGLXPixmap);
		xcb_free_pixmap(mRendererXcbConn, texInfo.mCompositePixmap);

		xcb_pixmap_t compositePixmap = xcb_generate_id(mRendererXcbConn);
		xcb_composite_name_window_pixmap(
			mRendererXcbConn, texInfo.mWindow, compositePixmap
		);

		xcb_glx_pixmap_t glxPixmap = xcb_generate_id(mRendererXcbConn);
		xcb_glx_create_pixmap(
			mRendererXcbConn, 0, req.mFBConfigId, compositePixmap, glxPixmap,
			BX_COUNTOF(GLX_PIXMAP_ATTRS) / 2,
			GLX_PIXMAP_ATTRS
		);

		texInfo.mCompositePixmap = compositePixmap;
		texInfo.mGLXPixmap = glxPixmap;
	}

	Display* mDisplay;
	Display* mRendererDisplay;
	SDL_Window* mWindow;
	xcb_connection_t* mXcbConn;
	xcb_connection_t* mRendererXcbConn;
	std::unordered_map<WindowId, WindowInfo> mWindows;
	bx::SpScUnboundedQueue<TextureReq> mTextureReqs;
	std::vector<TextureReq> mDeferredTextureReqs;
	std::unordered_map<uint16_t, TextureInfo> mTextures;
	unsigned int mEventIndex;
	std::vector<WindowEvent> mEvents;
	std::vector<WindowEvent> mTmpEventBuff;
};

#if BX_PLATFORM_LINUX == 1

static XWindow gXWindowInstance;

DesktopEnvironment* DesktopEnvironment::getInstance()
{
	return &gXWindowInstance;
}

#endif

}
