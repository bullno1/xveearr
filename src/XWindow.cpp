#include <bx/platform.h>

#if BX_PLATFORM_LINUX == 1

#include "IWindowSystem.hpp"
#include <unordered_map>
#include <vector>
#include <SDL_syswm.h>
#include <SDL.h>
#include <bx/spscqueue.h>
#include <bx/macros.h>
#include <bgfx/bgfxplatform.h>
#include <bgfx/bgfx.h>
#include <X11/Xlib-xcb.h>
#include <xcb/xcbext.h>
#include <xcb/composite.h>
#include <xcb/xcb_util.h>
#include <xcb/res.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xfixes.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>
#include "Registry.hpp"
#include "Log.hpp"

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
		mglXBindTexImageEXT = (PFNGLXBINDTEXIMAGEEXTPROC)glXGetProcAddress(
			(const GLubyte*)"glXBindTexImageEXT"
		);
		mglXReleaseTexImageEXT = (PFNGLXRELEASETEXIMAGEEXTPROC)glXGetProcAddress(
			(const GLubyte*)"glXReleaseTexImageEXT"
		);
		XVR_ENSURE(
			mglXBindTexImageEXT && mglXReleaseTexImageEXT,
			"GLX_EXT_texture_from_pixmap is not available"
		);

		mDisplay = XOpenDisplay(NULL);
		XVR_ENSURE(mDisplay, "Could not open display");

		mXcbConn = XGetXCBConnection(mDisplay);
		XSetEventQueueOwner(mDisplay, XCBOwnsEventQueue);

		xcb_prefetch_extension_data(mXcbConn, &xcb_composite_id);
		xcb_prefetch_extension_data(mXcbConn, &xcb_res_id);
		xcb_prefetch_extension_data(mXcbConn, &xcb_xfixes_id);

		const xcb_query_extension_reply_t* xcomposite =
			xcb_get_extension_data(mXcbConn, &xcb_composite_id);
		XVR_ENSURE(xcomposite->present, xcb_composite_id.name, " is not available");

		const xcb_query_extension_reply_t* xres =
			xcb_get_extension_data(mXcbConn, &xcb_res_id);
		XVR_ENSURE(xres->present, xcb_res_id.name, " is not available");

		const xcb_query_extension_reply_t* xfixes =
			xcb_get_extension_data(mXcbConn, &xcb_xfixes_id);
		XVR_ENSURE(xfixes->present, xcb_xfixes_id.name, " is not available");
		mXFixesFirstEvent = xfixes->first_event;

		xcb_xfixes_query_version_unchecked(
			mXcbConn,
			XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION
		);
		xcb_res_query_version_unchecked(
			mXcbConn,
			XCB_RES_MAJOR_VERSION, XCB_RES_MINOR_VERSION
		);

		SDL_SysWMinfo wmi;
		SDL_GetVersion(&wmi.version);
		SDL_GetWindowWMInfo(cfg.mWindow, &wmi);
		mRendererDisplay = wmi.info.x11.display;
		mRendererXcbConn = XGetXCBConnection(mRendererDisplay);
		mPID = getClientPidFromWindow(wmi.info.x11.window);

		xcb_generic_error_t *error;
		xcb_void_cookie_t voidCookie = xcb_grab_server_checked(mXcbConn);
		if((error = xcb_request_check(mXcbConn, voidCookie)))
		{
			free(error);
			XVR_LOG(Error, "Could not grab server");
			return false;
		}

		int numScreens = xcb_setup_roots_length(xcb_get_setup(mXcbConn));
		std::vector<xcb_void_cookie_t> cookies;
		cookies.reserve(numScreens * 2);

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

			cookies.push_back(
				xcb_xfixes_select_cursor_input_checked(
					mXcbConn,
					rootWindow,
					XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR
				)
			);
		}

		for(xcb_void_cookie_t cookie: cookies)
		{
			if((error = xcb_request_check(mXcbConn, cookie)))
			{
				free(error);
				XVR_LOG(Error, "Some request failed");
				return false;
			}
		}

		xcb_ewmh_connection_t ewmh;
		uint8_t initStatus = xcb_ewmh_init_atoms_replies(
			&ewmh, xcb_ewmh_init_atoms(mXcbConn, &ewmh), NULL
		);
		if(!initStatus)
		{
			xcb_ewmh_connection_wipe(&ewmh);
			XVR_LOG(Error, "Could not initialize EWMH atoms");
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
			XVR_LOG(Error, "EWMH is not supported");
			return false;
		}
		xcb_ewmh_connection_wipe(&ewmh);

		mWindowMgrPid = getPidFromWindow(supportWindow);
		XVR_ENSURE(mWindowMgrPid, "Could not retrieve PID of window manager");

		voidCookie = xcb_ungrab_server_checked(mXcbConn);
		if((error = xcb_request_check(mXcbConn, voidCookie)))
		{
			free(error);
			XVR_LOG(Error, "Could not ungrab server");
			return false;
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
			uint8_t respType = XCB_EVENT_RESPONSE_TYPE(xcbEvent);
			if(respType == mXFixesFirstEvent + XCB_XFIXES_CURSOR_NOTIFY)
			{
				updateCursorInfo(
					(xcb_xfixes_cursor_notify_event_t*)xcbEvent
				);
			}
			else
			{
				switch(respType)
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
		xcb_composite_query_version_unchecked(
			mRendererXcbConn,
			XCB_COMPOSITE_MAJOR_VERSION, XCB_COMPOSITE_MINOR_VERSION
		);

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

	CursorInfo getCursorInfo()
	{
		if(mCursors.empty())
		{
			retrieveCurrentCursor();
		}

		auto itr = mCursors.find(mCurrentCursor);
		if(itr == mCursors.end())
		{
			CursorInfo invalid;
			invalid.mTexture = BGFX_INVALID_HANDLE;
			return invalid;
		}
		else
		{
			return itr->second;
		}
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
		if(!queryTreeReply) { return 0; }

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
		XVR_ENSURE(geomReply, "Could not retrieve window's geometry");

		xcb_get_geometry_reply_t geom = *geomReply;
		free(geomReply);
		XVR_ENSURE(geom.depth != 0, "Window has zero depth");

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
		XVR_LOG(Debug,
			"Binding window ", std::hex, req.mWindow, std::dec,
			" to texture ", req.mBgfxHandle.idx);

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
		mglXBindTexImageEXT(
			mRendererDisplay, glxPixmap, GLX_FRONT_LEFT_EXT, NULL
		);

		TextureInfo texInfo;
		texInfo.mWindow = req.mWindow;
		texInfo.mGLHandle = glTexture;
		texInfo.mCompositePixmap = compositePixmap;
		texInfo.mGLXPixmap = glxPixmap;
		mTextures.insert(std::make_pair(req.mBgfxHandle.idx, texInfo));

		bgfx::overrideInternal(req.mBgfxHandle, glTexture);

		XVR_LOG(Debug,
			"Window ", std::hex, req.mWindow, std::dec,
			" bound  to texture ", req.mBgfxHandle.idx);
	}

	void unbindTexture(const TextureReq& req)
	{
		XVR_LOG(Debug, "Unbinding texture ", req.mBgfxHandle.idx);

		auto itr = mTextures.find(req.mBgfxHandle.idx);
		if(itr == mTextures.end()) { return; }

		TextureInfo& texInfo = itr->second;
		glBindTexture(GL_TEXTURE_2D, texInfo.mGLHandle);
		mglXReleaseTexImageEXT(
			mRendererDisplay, texInfo.mGLXPixmap, GLX_FRONT_LEFT_EXT
		);
		glXDestroyPixmap(mRendererDisplay, texInfo.mGLXPixmap);
		xcb_free_pixmap(mRendererXcbConn, texInfo.mCompositePixmap);
		glDeleteTextures(1, &texInfo.mGLHandle);
		mTextures.erase(itr);

		XVR_LOG(Debug, "Texture ", req.mBgfxHandle.idx, " unbound");
	}

	void rebindTexture(const TextureReq& req)
	{
		auto itr = mTextures.find(req.mBgfxHandle.idx);
		if(itr == mTextures.end()) { return; }

		XVR_LOG(Debug, "Rebinding texture ", req.mBgfxHandle.idx);

		TextureInfo& texInfo = itr->second;

		xcb_pixmap_t compositePixmap;
		GLXFBConfig fbConfig;
		if(!getCompositePixmap(texInfo.mWindow, compositePixmap, fbConfig))
		{
			return;
		}

		glBindTexture(GL_TEXTURE_2D, texInfo.mGLHandle);
		mglXReleaseTexImageEXT(
			mRendererDisplay, texInfo.mGLXPixmap,GLX_FRONT_LEFT_EXT
		);
		glXDestroyPixmap(mRendererDisplay, texInfo.mGLXPixmap);
		xcb_free_pixmap(mRendererXcbConn, texInfo.mCompositePixmap);

		GLXPixmap glxPixmap = glXCreatePixmap(
			mRendererDisplay, fbConfig, compositePixmap, GLX_PIXMAP_ATTRS
		);
		mglXBindTexImageEXT(
			mRendererDisplay, glxPixmap, GLX_FRONT_LEFT_EXT, NULL
		);

		texInfo.mCompositePixmap = compositePixmap;
		texInfo.mGLXPixmap = glxPixmap;

		XVR_LOG(Debug, "Texture ", req.mBgfxHandle.idx, " rebound");
	}

	bool getCompositePixmap(
		xcb_window_t window,
		xcb_pixmap_t& compositePixmap,
		GLXFBConfig& fbConfig
	)
	{
		// xcb_request_check will hang here for some reason so it cannot be
		// pipelined with the later get_geometry request
		xcb_pixmap_t pixmap = xcb_generate_id(mRendererXcbConn);
		xcb_generic_error_t* namePixmapError = xcb_request_check(
			mRendererXcbConn,
			xcb_composite_name_window_pixmap_checked(
				mRendererXcbConn, window, pixmap
			)
		);
		if(namePixmapError)
		{
			XVR_LOG(Error,
				"Could not assign composite pixmap to window ",
				std::hex, window, std::dec, ": ",
				xcb_event_get_error_label(namePixmapError->error_code)
			);
			free(namePixmapError);
			return false;
		}
		free(namePixmapError);

		xcb_get_geometry_reply_t* geomReply = xcb_get_geometry_reply(
			mRendererXcbConn,
			xcb_get_geometry(mRendererXcbConn, window),
			NULL
		);
		if(geomReply == NULL || geomReply->depth == 0)
		{
			free(geomReply);
			XVR_LOG(Error, "Could not retrieve window's data");
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
		XVR_LOG(Error, "Could not find a suitable FBConfig");
		return false;
	}

	void updateCursorInfo(xcb_xfixes_cursor_notify_event_t* ev)
	{
		XVR_LOG(Debug, "Cursor serial: ", ev->cursor_serial);
		if(mCursors.find(ev->cursor_serial) == mCursors.end())
		{
			retrieveCurrentCursor();
		}
		else
		{
			XVR_LOG(Debug, "Using cached cursor");
			mCurrentCursor = ev->cursor_serial;
		}
	}

	void retrieveCurrentCursor()
	{
		XVR_LOG(Debug, "Retrieving current cursor");

		xcb_xfixes_get_cursor_image_reply_t* cursorImage =
			xcb_xfixes_get_cursor_image_reply(
				mXcbConn, xcb_xfixes_get_cursor_image(mXcbConn), NULL
			);
		if(!cursorImage) { return; }

		CursorInfo cursorInfo;
		cursorInfo.mOriginX = cursorImage->xhot;
		cursorInfo.mOriginY = cursorImage->yhot;
		cursorInfo.mWidth = cursorImage->width;
		cursorInfo.mHeight = cursorImage->height;

		uint32_t imgSize = (uint32_t)cursorImage->width * (uint32_t)cursorImage->height;
		const uint32_t* imageData =
			xcb_xfixes_get_cursor_image_cursor_image(cursorImage);
		const bgfx::Memory* imgBuff = bgfx::alloc(imgSize * sizeof(uint32_t));
		for(uint32_t i = 0; i < imgSize; ++i)
		{
			uint32_t pixel = imageData[i];
			uint8_t a = (pixel >> 24) & 0xff;
			uint8_t r = (pixel >> 16) & 0xff;
			uint8_t g = (pixel >>  8) & 0xff;
			uint8_t b = (pixel >>  0) & 0xff;
			imgBuff->data[i * 4 + 0] = r;
			imgBuff->data[i * 4 + 1] = g;
			imgBuff->data[i * 4 + 2] = b;
			imgBuff->data[i * 4 + 3] = a;
		}
		cursorInfo.mTexture = bgfx::createTexture2D(
			cursorImage->width, cursorImage->height, 0,
			bgfx::TextureFormat::RGBA8,
			BGFX_TEXTURE_U_CLAMP|BGFX_TEXTURE_V_CLAMP,
			imgBuff
		);

		mCurrentCursor = cursorImage->cursor_serial;
		mCursors.insert(
			std::make_pair(cursorImage->cursor_serial, cursorInfo)
		);

		free(cursorImage);
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
	std::unordered_map<uint32_t, CursorInfo> mCursors;
	uint32_t mCurrentCursor;
	uint8_t mXFixesFirstEvent;
	PFNGLXBINDTEXIMAGEEXTPROC mglXBindTexImageEXT;
	PFNGLXRELEASETEXIMAGEEXTPROC mglXReleaseTexImageEXT;
};

XVR_REGISTER(IWindowSystem, XWindow)

}

#endif
