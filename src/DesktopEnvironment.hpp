#ifndef XVEEARR_DESKTOP_ENVIRONMENT_HPP
#define XVEEARR_DESKTOP_ENVIRONMENT_HPP

#include <cstdint>

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
	virtual void enumerateWindows(WindowEnumFn enumFn, void* context) = 0;
	virtual uintptr_t createTextureForWindow(WindowId wndId) = 0;
	virtual void endRender() = 0;

	static DesktopEnvironment& getInstance() { return *sInstance; }

private:
	static DesktopEnvironment* sInstance;
};

}

#endif
