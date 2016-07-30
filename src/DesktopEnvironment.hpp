#ifndef XVEEARR_DESKTOP_ENVIRONMENT_HPP
#define XVEEARR_DESKTOP_ENVIRONMENT_HPP

#include <cstdint>
#include <bgfx/bgfx.h>

#define XVEEARR_DECLARE_DE(CLASS) \
	namespace xveearr { \
		CLASS gDEInstance; \
		DesktopEnvironment* DesktopEnvironment::sInstance = &gDEInstance; \
	}

namespace xveearr
{

struct ApplicationContext;

typedef uintptr_t WindowId;

typedef void(*WindowEnumFn)(WindowId, void*);

class DesktopEnvironment
{
public:
	virtual bool init(const ApplicationContext& appCtx) = 0;
	virtual void shutdown() = 0;
	virtual void update() = 0;
	virtual void beginRender() = 0;
	virtual void endRender() = 0;
	virtual bgfx::TextureHandle getTexture(WindowId wndId) = 0;
	virtual void enumerateWindows(WindowEnumFn enumFn, void* context) = 0;
	virtual void getWindowSize(
		WindowId wndId, unsigned int& width, unsigned int& height) = 0;

	static DesktopEnvironment& getInstance() { return *sInstance; }

private:
	static DesktopEnvironment* sInstance;
};

}

#endif
