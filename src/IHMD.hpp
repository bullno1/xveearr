#ifndef XVEEARR_HMD_HPP
#define XVEEARR_HMD_HPP

#include <bgfx/bgfx.h>
#include "IComponent.hpp"
#include "IRenderHook.hpp"

namespace xveearr
{

struct RenderData
{
	bgfx::FrameBufferHandle mFrameBuffer;
	float mViewTransform[16];
	float mViewProjection[16];
};

struct Eye
{
	enum Enum
	{
		Left,
		Right,

		Count
	};
};

class IHMD: public IComponent, public IRenderHook
{
public:
	virtual void prepareResources() = 0;
	virtual void releaseResources() = 0;
	virtual void getViewportSize(unsigned int& widht, unsigned int& height) = 0;
	virtual void update() = 0;
	virtual const RenderData& getRenderData(Eye::Enum eye) = 0;
};

}

#endif
