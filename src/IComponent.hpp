#ifndef XVEEEARR_COMPONENT_HPP
#define XVEEEARR_COMPONENT_HPP

struct SDL_Window;

namespace xveearr
{

struct ApplicationContext
{
	int mArgc;
	const char** mArgv;
	SDL_Window* mWindow;
};

class IComponent
{
public:
	virtual bool init(const ApplicationContext& appCtx) = 0;
	virtual void shutdown() = 0;
	virtual const char* getName() const = 0;
};

}

#endif
