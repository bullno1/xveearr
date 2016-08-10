#ifndef XVEEARR_CONTROLLER_HPP
#define XVEEARR_CONTROLLER_HPP

#include <bgfx/bgfx.h>
#include "IComponent.hpp"

struct SDL_Window;

namespace xveearr
{

class IWindowManager;
class IHMD;

struct ControllerCfg
{
	SDL_Window* mWindow;
	IWindowManager* mWindowManager;
	IHMD* mHMD;
};

class IController: public IComponent<ControllerCfg>
{
public:
	virtual void update() = 0;
};

}

#endif
