#ifndef XVEEARR_WINDOW_SYSTEM_HPP
#define XVEEARR_WINDOW_SYSTEM_HPP

#include <cstdint>
#include <bgfx/bgfx.h>
#include "IComponent.hpp"
#include "IRenderHook.hpp"

struct SDL_Window;

namespace xveearr
{

typedef uintptr_t WindowId;
typedef uintptr_t PID;

struct WindowInfo
{
	bgfx::TextureHandle mTexture;
	bool mInvertedY;
	PID mPID;
	unsigned int mX;
	unsigned int mY;
	unsigned int mWidth;
	unsigned int mHeight;
};

struct CursorInfo
{
	bgfx::TextureHandle mTexture;
	unsigned int mOriginX;
	unsigned int mOriginY;
	unsigned int mWidth;
	unsigned int mHeight;
};

struct WindowEvent
{
	enum Type
	{
		WindowAdded,
		WindowRemoved,
		WindowUpdated,

		Count
	};

	Type mType;
	WindowId mWindow;
	WindowInfo mInfo;
};

struct WindowSystemCfg
{
	SDL_Window* mWindow;
};

class IWindowSystem: public IComponent<WindowSystemCfg>, public IRenderHook
{
public:
	virtual bool pollEvent(WindowEvent& event) = 0;
	virtual const WindowInfo* getWindowInfo(WindowId id) = 0;
	virtual CursorInfo getCursorInfo() = 0;
};

}

#endif
