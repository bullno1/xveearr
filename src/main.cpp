#include <unordered_map>
#include <SDL_syswm.h>
#include <SDL.h>
#include <bgfx/bgfxplatform.h>
#include <bgfx/bgfx.h>
#include <bx/bx.h>
#include <bx/thread.h>
#include <bx/sem.h>
#include <bx/fpumath.h>
#include "shaders/quad.vsh.h"
#include "shaders/quad.fsh.h"
#include "DesktopEnvironment.hpp"

namespace xveearr
{

namespace
{

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
	WindowInfo mInfo;
	float mTransform[16];
};

}

class Application
{
public:
	int run(int argc, const char* argv[])
	{
		if(!init(argc, argv))
		{
			shutdown();
			return EXIT_FAILURE;
		}

		int exitCode = mainLoop();
		shutdown();

		return exitCode;
	}

private:
	bool init(int argc, const char* argv[])
	{
		if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { return 1; }

		mWindow = SDL_CreateWindow(
			"Xveearr",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			1280, 720,
			SDL_WINDOW_SHOWN
		);

		if(!mWindow) { return false; }

		mDesktopEnvironment = DesktopEnvironment::getInstance();
		if(!mDesktopEnvironment) { return false; }

		ApplicationContext appCtx;
		appCtx.mArgc = argc;
		appCtx.mArgv = argv;
		appCtx.mWindow = mWindow;
		if(!mDesktopEnvironment->init(appCtx)) { return false; }

		mRenderThread.init(renderThread, this, 0, "Render thread");
		mRenderThreadReadySem.wait();

		bgfx::sdlSetWindow(mWindow);
		if(!bgfx::init()) { return false; }

		int width, height;
		SDL_GetWindowSize(appCtx.mWindow, &width, &height);
		bgfx::reset(width, height, BGFX_RESET_VSYNC);
		bgfx::setDebug(BGFX_DEBUG_TEXT);
		bgfx::setViewRect(0, 0, 0, width, height);
		bgfx::setViewClear(
			0,
			BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,
			0x303030ff,
			1.0f,
			0
		);

		float view[16];
		float eye[] = { 0.f, 0.f, 300.f };
		float at[] = { -300.f, 100.f, 0.f };
		bx::mtxLookAtRh(view, eye, at);

		float proj[16];
		bx::mtxProjRh(proj,
			50.0f,
			(float)width / (float)height,
			1.0f, 100000.f
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

		return true;
	}

	void shutdown()
	{
		bgfx::destroyUniform(mTextureUniform);
		bgfx::destroyProgram(mProgram);
		bgfx::destroyIndexBuffer(mQuadIndices);
		bgfx::destroyVertexBuffer(mQuad);

		bgfx::shutdown();
		if(mRenderThread.isRunning()) { mRenderThread.shutdown(); }

		mDesktopEnvironment->shutdown();

		SDL_DestroyWindow(mWindow);
		SDL_Quit();
	}

	int mainLoop()
	{
		while(true)
		{
			SDL_Event sdlEvent;
			while(SDL_PollEvent(&sdlEvent))
			{
				if(sdlEvent.type == SDL_QUIT) { return 0; }
			}

			WindowEvent windowEvent;
			while(mDesktopEnvironment->pollEvent(windowEvent))
			{
				switch(windowEvent.mType)
				{
					case WindowEvent::WindowAdded:
						onWindowsAdded(windowEvent);
						break;
					case WindowEvent::WindowRemoved:
						onWindowsRemoved(windowEvent);
						break;
				}
			}

			for(auto&& pair: mWindows)
			{
				const WindowData& wndData = pair.second;
				bgfx::setTransform(wndData.mTransform);
				bgfx::setVertexBuffer(mQuad);
				bgfx::setIndexBuffer(mQuadIndices);
				bgfx::setTexture(0, mTextureUniform, wndData.mInfo.mTexture);
				bgfx::submit(0, mProgram);
			}

			bgfx::touch(0);
			bgfx::frame();
		}
	}

	void onWindowsAdded(const WindowEvent& event)
	{
		WindowData wndData;
		wndData.mInfo = event.mInfo;
		bx::mtxSRT(wndData.mTransform,
			event.mInfo.mWidth, event.mInfo.mHeight, 1.0f,
			0.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f
		);
		mWindows.insert(std::make_pair(event.mId, wndData));
	}

	void onWindowsRemoved(const WindowEvent& event)
	{
		mWindows.erase(event.mId);
	}

	static int32_t renderThread(void* userData)
	{
		Application* app = static_cast<Application*>(userData);

		// Ensure that this thread is registered as the render thread before
		// bgfx is initialized
		bgfx::renderFrame();
		app->mRenderThreadReadySem.post();

		while(true)
		{
			app->mDesktopEnvironment->beginRender();
			bgfx::RenderFrame::Enum renderStatus = bgfx::renderFrame();
			app->mDesktopEnvironment->endRender();

			if(renderStatus == bgfx::RenderFrame::Exiting)
			{
				break;
			}
		}

		return 0;
	}

	SDL_Window* mWindow;
	DesktopEnvironment* mDesktopEnvironment;
	bx::Thread mRenderThread;
	bx::Semaphore mRenderThreadReadySem;
	bgfx::VertexBufferHandle mQuad;
	bgfx::IndexBufferHandle mQuadIndices;
	bgfx::ProgramHandle mProgram;
	bgfx::UniformHandle mTextureUniform;
	std::unordered_map<WindowId, WindowData> mWindows;
};

}

int main(int argc, const char* argv[])
{
	xveearr::Application app;
	return app.run(argc, argv);
}
