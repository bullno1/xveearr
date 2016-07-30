#ifndef XVEEARR_APPLICATION_HPP
#define XVEEARR_APPLICATION_HPP

#include <SDL.h>

#define XVEEARR_DECLARE_APP(CLASS) \
	namespace xveearr { \
		CLASS gAppInstance; \
		Application* Application::sInstance = &gAppInstance; \
	}

namespace xveearr
{

struct ApplicationContext
{
	int mArgc;
	const char** mArgv;
	SDL_Window* mWindow;
};

class Application
{
public:
	virtual bool init(const ApplicationContext& appCtx) = 0;
	virtual bool onEvent(const SDL_Event& event) = 0;
	virtual bool update() = 0;
	virtual void render() = 0;
	virtual void shutdown() = 0;

	static Application& getInstance() { return *sInstance; }

private:
	static Application* sInstance;
};

}

#endif
