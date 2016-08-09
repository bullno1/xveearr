#ifndef XVEEARR_WINDOW_SYSTEM_HPP
#define XVEEARR_WINDOW_SYSTEM_HPP

#include <cstdint>
#include <bgfx/bgfx.h>
#include "IComponent.hpp"
#include "IRenderHook.hpp"

namespace xveearr
{

typedef uintptr_t WindowId;

struct WindowInfo
{
	bgfx::TextureHandle mTexture;
	bool mInvertedY;
	unsigned int mX;
	unsigned int mY;
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

typedef void(*EnumWindowFn)(WindowId id, const WindowInfo& info, void* context);

class IWindowSystem: public IComponent, public IRenderHook
{
public:
	virtual bool pollEvent(WindowEvent& event) = 0;
	virtual const WindowInfo* getWindowInfo(WindowId id) = 0;
};

}

#endif
