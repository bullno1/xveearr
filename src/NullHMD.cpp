#include "IHMD.hpp"
#include <bx/fpumath.h>
#include <SDL.h>
#include "Registry.hpp"

namespace xveearr
{

namespace
{

const unsigned int gViewportWidth = 1280 / 2;
const unsigned int gViewportHeight = 720;
const float gLeftEye[] = { -0.03f, 0.f, 0.f };
const float gRightEye[] = { 0.03f, 0.f, 0.f };
float gLookAt[] = { 0.f, 0.f, -0.5f };

}

class NullHMD: public IHMD
{
public:
	NullHMD()
	{
		mRenderData[Eye::Left].mFrameBuffer = BGFX_INVALID_HANDLE;
		mRenderData[Eye::Right].mFrameBuffer = BGFX_INVALID_HANDLE;
	}

	bool init()
	{
		bx::mtxIdentity(mHeadTransform);

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
		mRenderData[Eye::Left].mFrameBuffer = createEyeFB();
		mRenderData[Eye::Right].mFrameBuffer = createEyeFB();
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

	void getHeadTransform(float* headTransform)
	{
		memcpy(headTransform, mHeadTransform, sizeof(mHeadTransform));
	}

	const char* getName() const
	{
		return "null";
	}

	void update()
	{
		const Uint8* keyStates = SDL_GetKeyboardState(NULL);
		float translation[3] = {};
		float rotation[3] = {};

		if(keyStates[SDL_SCANCODE_A]) { translation[0] = -0.004f; }
		if(keyStates[SDL_SCANCODE_D]) { translation[0] =  0.004f; }
		if(keyStates[SDL_SCANCODE_W]) { translation[2] = -0.004f; }
		if(keyStates[SDL_SCANCODE_S]) { translation[2] =  0.004f; }
		if(keyStates[SDL_SCANCODE_Q]) { rotation[1] = -0.01f; }
		if(keyStates[SDL_SCANCODE_E]) { rotation[1] =  0.01f; }
		if(keyStates[SDL_SCANCODE_R]) { rotation[0] = -0.01f; }
		if(keyStates[SDL_SCANCODE_F]) { rotation[0] =  0.01f; }

		float move[16];
		bx::mtxSRT(move,
			1.f, 1.f, 1.f,
			rotation[0], rotation[1], rotation[2],
			translation[0], translation[1], translation[2]
		);
		float tmp[16];
		memcpy(tmp, mHeadTransform, sizeof(tmp));
		bx::mtxMul(mHeadTransform, move, tmp);

		float leftEye[3];
		float leftLookAt[3];
		float leftRelLookat[3];
		bx::vec3MulMtx(leftEye, gLeftEye, mHeadTransform);
		bx::vec3Add(leftRelLookat, gLeftEye, gLookAt);
		bx::vec3MulMtx(leftLookAt, leftRelLookat, mHeadTransform);
		bx::mtxLookAtRh(
			mRenderData[Eye::Left].mViewTransform, leftEye, leftLookAt
		);
		bx::mtxProjRh(mRenderData[Eye::Left].mViewProjection,
			60.0f,
			(float)gViewportWidth / (float)gViewportHeight,
			0.15f, 10.f
		);

		float rightEye[3];
		float rightLookAt[3];
		float rightRelLookat[3];
		bx::vec3MulMtx(rightEye, gRightEye, mHeadTransform);
		bx::vec3Add(rightRelLookat, gRightEye, gLookAt);
		bx::vec3MulMtx(rightLookAt, rightRelLookat, mHeadTransform);
		bx::mtxLookAtRh(
			mRenderData[Eye::Right].mViewTransform, rightEye, rightLookAt
		);
		bx::mtxProjRh(mRenderData[Eye::Right].mViewProjection,
			60.0f,
			(float)gViewportWidth / (float)gViewportHeight,
			0.15f, 10.f
		);
	}

	const RenderData& getRenderData(Eye::Enum eye)
	{
		return mRenderData[eye];
	}

private:
	bgfx::FrameBufferHandle createEyeFB()
	{
		bgfx::TextureHandle textures[] = {
			bgfx::createTexture2D(
				gViewportWidth, gViewportHeight, 1,
				bgfx::TextureFormat::BGRA8,
				BGFX_TEXTURE_RT|BGFX_TEXTURE_U_CLAMP|BGFX_TEXTURE_V_CLAMP
			),
			bgfx::createTexture2D(
				gViewportWidth, gViewportHeight, 1,
				bgfx::TextureFormat::D16F,
				BGFX_TEXTURE_RT_WRITE_ONLY
			)
		};

		return bgfx::createFrameBuffer(BX_COUNTOF(textures), textures, true);
	}

	float mHeadTransform[16];
	RenderData mRenderData[Eye::Count];
};

XVR_REGISTER(IHMD, NullHMD)

}
