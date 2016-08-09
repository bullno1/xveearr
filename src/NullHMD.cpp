#include "IHMD.hpp"
#include <bx/fpumath.h>
#include "Registry.hpp"

namespace xveearr
{

namespace
{

const unsigned int gViewportWidth = 1280 / 2;
const unsigned int gViewportHeight = 720;

}

class NullHMD: public IHMD
{
public:
	NullHMD()
	{
		mRenderData[Eye::Left].mFrameBuffer = BGFX_INVALID_HANDLE;
		mRenderData[Eye::Right].mFrameBuffer = BGFX_INVALID_HANDLE;
	}

	bool init(const ApplicationContext&)
	{
		return true;
	}

	void shutdown()
	{
	}

	void initRenderer()
	{
	}

	void shutdownRenderer()
	{
	}

	void beginRender()
	{
	}

	void endRender()
	{
	}

	void prepareResources()
	{
		mRenderData[Eye::Left].mFrameBuffer = bgfx::createFrameBuffer(
			gViewportWidth, gViewportHeight,
			bgfx::TextureFormat::RGBA8
		);
		mRenderData[Eye::Right].mFrameBuffer = bgfx::createFrameBuffer(
			gViewportWidth, gViewportHeight,
			bgfx::TextureFormat::RGBA8
		);

		float leftEye[] = { -50.0f, 0.0f, 600.f };
		float leftLookAt[] = { -50.0f, 0.0f, 0.f };
		bx::mtxLookAtRh(
			mRenderData[Eye::Left].mViewTransform, leftEye, leftLookAt
		);
		bx::mtxProjRh(mRenderData[Eye::Left].mViewProjection,
			50.0f,
			(float)gViewportWidth / (float)gViewportHeight,
			1.f, 100000.f
		);

		float rightEye[] = { 50.0f, 0.0f, 600.f };
		float rightLookAt[] = { 50.0f, 0.0f, 0.f };
		bx::mtxLookAtRh(
			mRenderData[Eye::Right].mViewTransform, rightEye, rightLookAt
		);
		bx::mtxProjRh(mRenderData[Eye::Right].mViewProjection,
			50.0f,
			(float)gViewportWidth / (float)gViewportHeight,
			1.f, 100000.f
		);
	}

	void releaseResources()
	{
		if(bgfx::isValid(mRenderData[Eye::Left].mFrameBuffer))
		{
			bgfx::destroyFrameBuffer(mRenderData[Eye::Left].mFrameBuffer);
		}

		if(bgfx::isValid(mRenderData[Eye::Right].mFrameBuffer))
		{
			bgfx::destroyFrameBuffer(mRenderData[Eye::Right].mFrameBuffer);
		}
	}

	void getViewportSize(unsigned int& width, unsigned int& height)
	{
		width = gViewportWidth;
		height = gViewportHeight;
	}

	const char* getName() const
	{
		return "null";
	}

	void update()
	{
	}

	const RenderData& getRenderData(Eye::Enum eye)
	{
		return mRenderData[eye];
	}

private:
	RenderData mRenderData[Eye::Count];
};

NullHMD gNullHMDInstance;
Registry<IHMD>::Entry gFakeHMDEntry(&gNullHMDInstance);

}
