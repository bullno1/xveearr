#include "Application.hpp"
#include <cstdio>
#include <unordered_map>
#include <SDL_syswm.h>
#include <bgfx/bgfxplatform.h>
#include <bgfx/bgfx.h>
#include <bx/bx.h>
#include <bx/thread.h>
#include <bx/sem.h>
#include <bx/spscqueue.h>
#include <bx/fpumath.h>
#include "shaders/quad.vsh.h"
#include "shaders/quad.fsh.h"
#include "DesktopEnvironment.hpp"

namespace xveearr
{

namespace
{

struct TextureBind
{
	bgfx::TextureHandle mTexture;
	WindowId mWindowId;
};

struct Vertex
{
	float mX;
	float mY;

	static void init()
	{
		mDecl
			.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.end();
	}

	static bgfx::VertexDecl mDecl;
};

bgfx::VertexDecl Vertex::mDecl;

static Vertex gQuad[] =
{
	{-0.5f,  0.5f},
	{-0.5f, -0.5f},
	{ 0.5f, -0.5f},
	{ 0.5f,  0.5f}
};

static const uint16_t gIndicies[] =
{
	0, 1, 2,
	2, 3, 0
};

struct WindowData
{
	uint8_t mCounter;
	bgfx::TextureHandle mTexture;
};

}

class ApplicationImpl: public Application
{
public:
	ApplicationImpl()
		:mDesktopEnvironment(DesktopEnvironment::getInstance())
	{}

	bool init(const ApplicationContext& appCtx)
	{
		if(!mDesktopEnvironment.init(appCtx)) { return false; }

		mRenderThread.init(renderThread, this, 0, "Render thread");
		mRenderThreadReadySem.wait();

		bgfx::sdlSetWindow(appCtx.mWindow);
		if(!bgfx::init()) { return false; }

		int width, height;
		SDL_GetWindowSize(appCtx.mWindow, &width, &height);
		bgfx::reset(width, height, BGFX_RESET_VSYNC);
		bgfx::setDebug(BGFX_DEBUG_TEXT | BGFX_DEBUG_STATS);
		bgfx::setViewRect(0, 0, 0, width, height);
		bgfx::setViewClear(
			0,
			BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,
			0x303030ff,
			1.0f,
			0
		);

		float view[16];
		bx::mtxIdentity(view);
		float proj[16];
		bx::mtxOrtho(
			proj,
			width * -0.5f, width * 0.5f,
			height * -0.5f, height * 0.5f,
			-1.0f,
			1.0f
		);
		bgfx::setViewTransform(0, view, proj);

		Vertex::init();
		mQuad = bgfx::createVertexBuffer(
			bgfx::makeRef(gQuad, sizeof(gQuad)),
			Vertex::mDecl
		);
		mQuadIndices = bgfx::createIndexBuffer(
			bgfx::makeRef(gIndicies, sizeof(gIndicies))
		);
		bgfx::ShaderHandle vsh = bgfx::createShader(
			bgfx::makeRef(quad_vsh_h, sizeof(quad_vsh_h))
		);
		bgfx::ShaderHandle fsh = bgfx::createShader(
			bgfx::makeRef(quad_fsh_h, sizeof(quad_fsh_h))
		);
		mProgram = bgfx::createProgram(vsh, fsh, true);
		mTextureUniform = bgfx::createUniform(
			"u_texture", bgfx::UniformType::Int1
		);

		mCounter = 0;

		return true;
	}

	bool onEvent(const SDL_Event& event)
	{
		return event.type != SDL_QUIT;
	}

	bool update()
	{
		mDesktopEnvironment.update();
		return true;
	}

	void render()
	{
		++mCounter;

		bgfx::touch(0);
		mDesktopEnvironment.enumerateWindows(drawWindow, this);

		for(auto itr = mWindows.begin(); itr != mWindows.end();)
		{
			if(itr->second.mCounter != mCounter)
			{
				bgfx::destroyTexture(itr->second.mTexture);
				itr = mWindows.erase(itr);
			}
			else
			{
				++itr;
			}
		}

		bgfx::frame();
	}

	void shutdown()
	{
		bgfx::destroyUniform(mTextureUniform);
		bgfx::destroyProgram(mProgram);
		bgfx::destroyIndexBuffer(mQuadIndices);
		bgfx::destroyVertexBuffer(mQuad);
		bgfx::shutdown();
		if(mRenderThread.isRunning()) { mRenderThread.shutdown(); }

		mDesktopEnvironment.shutdown();
	}

private:
	static int32_t renderThread(void* userData)
	{
		ApplicationImpl* app = static_cast<ApplicationImpl*>(userData);

		// Ensure that this thread is registered as the render thread before
		// bgfx is initialized
		bgfx::renderFrame();
		app->mRenderThreadReadySem.post();

		while(true)
		{
			app->mDesktopEnvironment.beginRender();
			bgfx::RenderFrame::Enum renderStatus = bgfx::renderFrame();
			app->mDesktopEnvironment.endRender();

			if(renderStatus == bgfx::RenderFrame::Render)
			{
				while(app->mBindQueue.peek())
				{
					TextureBind* bind = app->mBindQueue.pop();
					uintptr_t nativeTexture =
						app->mDesktopEnvironment.createTextureForWindow(
							bind->mWindowId
						);
					if(nativeTexture)
					{
						bgfx::overrideInternal(bind->mTexture, nativeTexture);
					}
					delete bind;
				}
			}

			if(renderStatus == bgfx::RenderFrame::Exiting)
			{
				break;
			}
		}

		return 0;
	}

	static void drawWindow(WindowId id, void* context)
	{
		ApplicationImpl* app = static_cast<ApplicationImpl*>(context);

		auto itr = app->mWindows.find(id);
		if(itr == app->mWindows.end())
		{
			WindowData data;
			data.mCounter = app->mCounter;
			itr = app->mWindows.insert(std::make_pair(id, data)).first;
			itr->second.mTexture = bgfx::createTexture2D(
				1, 1,
				0,
				bgfx::TextureFormat::RGBA8
			);
			TextureBind* bind = new TextureBind;
			bind->mTexture = itr->second.mTexture;
			bind->mWindowId = id;
			app->mBindQueue.push(bind);
		}
		itr->second.mCounter = app->mCounter;

		unsigned int width, height;
		app->mDesktopEnvironment.getWindowSize(id, width, height);
		float transform[16];
		bx::mtxIdentity(transform);
		bx::mtxScale(transform, width, height, 1.0f);
		bgfx::setTransform(transform);
		bgfx::setVertexBuffer(app->mQuad);
		bgfx::setIndexBuffer(app->mQuadIndices);
		bgfx::setTexture(0, app->mTextureUniform, itr->second.mTexture);
		bgfx::submit(0, app->mProgram);
	}

	DesktopEnvironment& mDesktopEnvironment;
	uint8_t mCounter;
	bx::Thread mRenderThread;
	bx::Semaphore mRenderThreadReadySem;
	bx::SpScUnboundedQueue<TextureBind> mBindQueue;
	bgfx::VertexBufferHandle mQuad;
	bgfx::IndexBufferHandle mQuadIndices;
	bgfx::ProgramHandle mProgram;
	bgfx::UniformHandle mTextureUniform;
	std::unordered_map<WindowId, WindowData> mWindows;
};

}

XVEEARR_DECLARE_APP(ApplicationImpl)
