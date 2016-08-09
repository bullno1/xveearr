#ifndef XVEEARR_RENDER_HOOK_HPP
#define XVEEARR_RENDER_HOOK_HPP

namespace xveearr
{

class IRenderHook
{
public:
	virtual void initRenderer() = 0;
	virtual void shutdownRenderer() = 0;
	virtual void beginRender() = 0;
	virtual void endRender() = 0;
};

}

#endif
