#ifndef XVEEARR_DESKTOP_ENVIRONMENT_HPP
#define XVEEARR_DESKTOP_ENVIRONMENT_HPP

#include <cstdint>
#include <bgfx/bgfx.h>

struct SDL_Window;

namespace xveearr
{

struct ApplicationContext
{
	int mArgc;
	const char** mArgv;
	SDL_Window* mWindow;
};

typedef uintptr_t WindowId;

struct WindowInfo
{
	bgfx::TextureHandle mTexture;
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
	WindowId mId;
	WindowInfo mInfo;
};

class DesktopEnvironment
{
public:
	virtual bool init(const ApplicationContext& appCtx) = 0;
	virtual void shutdown() = 0;
	virtual bool pollEvent(WindowEvent& event) = 0;
	virtual void beginRender() = 0;
	virtual void endRender() = 0;
	virtual const WindowInfo* getWindowInfo(WindowId id) = 0;

	static DesktopEnvironment* getInstance();
};

}

#endif
