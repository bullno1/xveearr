#include "IWindowSystem.hpp"
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
#include <xcb/xcb_util.h>
#include <xcb/res.h>
#include <xcb/xcb_ewmh.h>
#include "Registry.hpp"
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
};

struct TextureInfo
{
	GLuint mGLHandle;
	xcb_window_t mWindow;
	xcb_pixmap_t mCompositePixmap;
	GLXPixmap mGLXPixmap;
};

const int GLX_PIXMAP_ATTRS[] = {
	GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
	GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGBA_EXT,
	None
};

}

class XWindow: public IWindowSystem
{
public:
	XWindow()
		:mDisplay(NULL)
		,mXcbConn(NULL)
		,mEventIndex(0)
	{}

	bool init(const WindowSystemCfg& cfg)
	{
		mDisplay = XOpenDisplay(NULL);
		if(mDisplay == NULL) { return false; }

		mXcbConn = XGetXCBConnection(mDisplay);
		XSetEventQueueOwner(mDisplay, XCBOwnsEventQueue);

		SDL_SysWMinfo wmi;
		SDL_GetVersion(&wmi.version);
		SDL_GetWindowWMInfo(cfg.mWindow, &wmi);
		mRendererDisplay = wmi.info.x11.display;
		mPID = getClientPidFromWindow(wmi.info.x11.window);

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

		xcb_ewmh_connection_t ewmh;
		uint8_t initStatus = xcb_ewmh_init_atoms_replies(
			&ewmh, xcb_ewmh_init_atoms(mXcbConn, &ewmh), NULL
		);
		if(!initStatus)
		{
			xcb_ewmh_connection_wipe(&ewmh);
			return false;
		}

		xcb_window_t supportWindow;
		uint8_t supportCheckStatus = xcb_ewmh_get_supporting_wm_check_reply(
			&ewmh,
			xcb_ewmh_get_supporting_wm_check(
				&ewmh,
				xcb_setup_roots_iterator(xcb_get_setup(mXcbConn)).data->root
			),
			&supportWindow,
			NULL
		);
		if(!supportCheckStatus)
		{
			xcb_ewmh_connection_wipe(&ewmh);
			return false;
		}
		xcb_ewmh_connection_wipe(&ewmh);

		mWindowMgrPid = getPidFromWindow(supportWindow);
		if(!mWindowMgrPid) { return false; }

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

	const char* getName() const
	{
		return "xwindow";
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
	}

	void endRender()
	{
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
	uint32_t getPidFromWindow(xcb_window_t window)
	{
		xcb_res_client_id_spec_t idSpecs;
		idSpecs.client = window;
		idSpecs.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;
		xcb_res_query_client_ids_reply_t* idReply =
			xcb_res_query_client_ids_reply(
				mXcbConn, xcb_res_query_client_ids(mXcbConn, 1, &idSpecs), NULL
			);

		if(!idReply) { return 0; }

		uint32_t pid =
			*xcb_res_client_id_value_value(
				(xcb_res_query_client_ids_ids_iterator(idReply).data)
			);
		free(idReply);

		return pid;
	}

	uint32_t getClientPidFromWindow(xcb_window_t window)
	{
		uint32_t pid = getPidFromWindow(window);

		if(pid == 0 || pid != mWindowMgrPid) { return pid; }

		xcb_query_tree_reply_t* queryTreeReply = xcb_query_tree_reply(
			mXcbConn, xcb_query_tree(mXcbConn, window), NULL
		);

		int numChildren = xcb_query_tree_children_length(queryTreeReply);
		xcb_window_t* children = xcb_query_tree_children(queryTreeReply);
		for(int i = 0; i < numChildren; ++i)
		{
			uint32_t pid = getClientPidFromWindow(children[i]);
			if(pid != 0)
			{
				free(queryTreeReply);
				return pid;
			}
		}

		free(queryTreeReply);
		return 0;
	}

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

		xcb_get_geometry_reply_t* geomReply = xcb_get_geometry_reply(
			mXcbConn, xcb_get_geometry(mXcbConn, event.mWindow), NULL
		);

		if(geomReply == NULL) { return false; }

		xcb_get_geometry_reply_t geom = *geomReply;
		free(geomReply);

		if(geom.depth == 0) { return false; }

		uint32_t clientPid = getClientPidFromWindow(event.mWindow);
		if(clientPid == 0 || clientPid == mPID) { return false; }

		bgfx::TextureHandle texture =
			bgfx::createTexture2D(1, 1, 0, bgfx::TextureFormat::RGBA8);

		WindowInfo wndInfo;
		wndInfo.mX = geom.x;
		wndInfo.mY = geom.y;
		wndInfo.mWidth = geom.width;
		wndInfo.mHeight = geom.height;
		wndInfo.mTexture = texture;
		wndInfo.mInvertedY = true;
		wndInfo.mPID = clientPid;
		event.mInfo = wndInfo;
		mWindows.insert(std::make_pair(event.mWindow, wndInfo));

		TextureReq* req = new TextureReq;
		req->mType = TextureReq::Bind;
		req->mBgfxHandle = texture;
		req->mWindow = event.mWindow;
		mTextureReqs.push(req);

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
		event.mInfo = wndInfo;

		if(oldWidth != event.mInfo.mWidth || oldHeight != event.mInfo.mHeight)
		{
			TextureReq* req = new TextureReq;
			req->mType = TextureReq::Rebind;
			req->mBgfxHandle = wndInfo.mTexture;
			mTextureReqs.push(req);
		}

		return true;
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
		xcb_pixmap_t compositePixmap;
		GLXFBConfig fbConfig;
		if(!getCompositePixmap(req.mWindow, compositePixmap, fbConfig))
		{
			return;
		}

		GLuint glTexture;
		glGenTextures(1, &glTexture);
		glBindTexture(GL_TEXTURE_2D, glTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		GLXPixmap glxPixmap = glXCreatePixmap(
			mRendererDisplay, fbConfig, compositePixmap, GLX_PIXMAP_ATTRS
		);
		glXBindTexImageEXT(
			mRendererDisplay, glxPixmap, GLX_FRONT_LEFT_EXT, NULL
		);

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
		glBindTexture(GL_TEXTURE_2D, texInfo.mGLHandle);
		glXReleaseTexImageEXT(
			mRendererDisplay, texInfo.mGLXPixmap, GLX_FRONT_LEFT_EXT
		);
		glXDestroyPixmap(mRendererDisplay, texInfo.mGLXPixmap);
		xcb_free_pixmap(mRendererXcbConn, texInfo.mCompositePixmap);
		glDeleteTextures(1, &texInfo.mGLHandle);
		mTextures.erase(itr);
	}

	void rebindTexture(const TextureReq& req)
	{
		auto itr = mTextures.find(req.mBgfxHandle.idx);
		if(itr == mTextures.end()) { return; }

		TextureInfo& texInfo = itr->second;

		xcb_pixmap_t compositePixmap;
		GLXFBConfig fbConfig;
		if(!getCompositePixmap(texInfo.mWindow, compositePixmap, fbConfig))
		{
			return;
		}

		glBindTexture(GL_TEXTURE_2D, texInfo.mGLHandle);
		glXReleaseTexImageEXT(
			mRendererDisplay, texInfo.mGLXPixmap,GLX_FRONT_LEFT_EXT
		);
		glXDestroyPixmap(mRendererDisplay, texInfo.mGLXPixmap);
		xcb_free_pixmap(mRendererXcbConn, texInfo.mCompositePixmap);

		GLXPixmap glxPixmap = glXCreatePixmap(
			mRendererDisplay, fbConfig, compositePixmap, GLX_PIXMAP_ATTRS
		);
		glXBindTexImageEXT(
			mRendererDisplay, glxPixmap, GLX_FRONT_LEFT_EXT, NULL
		);

		texInfo.mCompositePixmap = compositePixmap;
		texInfo.mGLXPixmap = glxPixmap;
	}

	bool getCompositePixmap(
		xcb_window_t window,
		xcb_pixmap_t& compositePixmap,
		GLXFBConfig& fbConfig
	)
	{
		xcb_pixmap_t pixmap = xcb_generate_id(mRendererXcbConn);
		xcb_void_cookie_t namePixmapCookie =
			xcb_composite_name_window_pixmap_checked(
				mRendererXcbConn, window, pixmap
			);
		xcb_get_geometry_cookie_t getGeomCookie = xcb_get_geometry(
			mRendererXcbConn, window
		);

		xcb_generic_error_t* namePixmapError = xcb_request_check(
			mRendererXcbConn, namePixmapCookie
		);
		xcb_get_geometry_reply_t* geomReply = xcb_get_geometry_reply(
			mRendererXcbConn, getGeomCookie, NULL
		);

		if(namePixmapError != NULL || geomReply == NULL || geomReply->depth == 0)
		{
			if(!namePixmapError)
			{
				xcb_free_pixmap(mRendererXcbConn, pixmap);
			}

			free(namePixmapError);
			free(geomReply);
			return false;
		}

		xcb_get_geometry_reply_t geom = *geomReply;
		free(geomReply);

		const int pixmapConfig[] = {
			GLX_BIND_TO_TEXTURE_RGBA_EXT, True,
			GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
			GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
			GLX_DOUBLEBUFFER, False,
			GLX_Y_INVERTED_EXT, (int)GLX_DONT_CARE,
			None
		};

		int numConfigs = 0;
		int screen = DefaultScreen(mRendererDisplay);
		GLXFBConfig* fbConfigs = glXChooseFBConfig(
			mRendererDisplay, screen, pixmapConfig, &numConfigs
		);

		for(int i = 0; i < numConfigs; ++i)
		{
			XVisualInfo* visualInfo =
				glXGetVisualFromFBConfig(mRendererDisplay, fbConfigs[i]);

			if(visualInfo->depth == geom.depth)
			{
				compositePixmap = pixmap;
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
	Display* mRendererDisplay;
	xcb_connection_t* mXcbConn;
	xcb_connection_t* mRendererXcbConn;
	PID mPID;
	uint32_t mWindowMgrPid;
	std::unordered_map<WindowId, WindowInfo> mWindows;
	bx::SpScUnboundedQueue<TextureReq> mTextureReqs;
	std::vector<TextureReq> mDeferredTextureReqs;
	std::unordered_map<uint16_t, TextureInfo> mTextures;
	unsigned int mEventIndex;
	std::vector<WindowEvent> mEvents;
	std::vector<WindowEvent> mTmpEventBuff;
};

#if BX_PLATFORM_LINUX == 1

XVR_REGISTER(IWindowSystem, XWindow)

#endif

}
