#include <unordered_map>
#include <list>
#include <vector>
#include <algorithm>
#include <SDL_syswm.h>
#include <SDL.h>
#include <bgfx/bgfxplatform.h>
#include <bgfx/bgfx.h>
#include <bx/bx.h>
#include <bx/thread.h>
#include <bx/sem.h>
#include <bx/fpumath.h>
#include <bx/commandline.h>
#include "config.h"
#include "shaders/quad.vsh.h"
#include "shaders/quad.fsh.h"
#include "IWindowManager.hpp"
#include "Registry.hpp"
#include "IWindowSystem.hpp"
#include "IHMD.hpp"
#include "IController.hpp"

XVR_DEFINE_REGISTRY(xveearr::IHMD)
XVR_DEFINE_REGISTRY(xveearr::IWindowSystem)
XVR_DEFINE_REGISTRY(xveearr::IController)

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
	{ 0.f,  0.f },
	{ 0.f, -1.f },
	{ 1.f, -1.f },
	{ 1.f,  0.f }
};

static const uint16_t gIndicies[] =
{
	0, 1, 2,
	2, 3, 0
};

struct RenderPass
{
	enum Enum {
		LeftEye,
		RightEye,
		Mirror,

		Count
	};
};

static const uint32_t gClearColor = 0x303030ff;

struct WindowGroup
{
	std::list<WindowId> mMembers;
	float mTransform[16];
};

}

class Application: IWindowManager
{
public:
	Application()
		:mHMD(NULL)
		,mWindow(NULL)
		,mBgfxInitialized(false)
		,mWindowSystem(NULL)
	{
		mQuad = BGFX_INVALID_HANDLE;
		mQuadIndices = BGFX_INVALID_HANDLE;
		mProgram = BGFX_INVALID_HANDLE;
		mTextureUniform = BGFX_INVALID_HANDLE;
		mQuadInfoUniform = BGFX_INVALID_HANDLE;
	}

	int run(int argc, const char* argv[])
	{
		bx::CommandLine cmdLine(argc, argv);
		if(cmdLine.hasArg("help"))
		{
			return showHelp();
		}
		else if(cmdLine.hasArg('v', "version"))
		{
			return showVersion();
		}

		if(!init(argc, argv))
		{
			shutdown();
			return EXIT_FAILURE;
		}

		int exitCode = mainLoop();
		shutdown();

		return exitCode;
	}

	unsigned int getWindowGroups(const PID** pids)
	{
		*pids = mPIDs.data();
		return mPIDs.size();
	}

	unsigned int getWindows(PID pid, const WindowId** wids)
	{
		mTmpWindows.clear();

		if(pid)
		{
			auto itr = mWindowGroups.find(pid);
			if(itr != mWindowGroups.end())
			{
				std::copy(
					itr->second.mMembers.begin(),
					itr->second.mMembers.end(),
					std::back_inserter(mTmpWindows)
				);
			}
		}
		else
		{
			for(auto&& pair: mWindows)
			{
				mTmpWindows.push_back(pair.first);
			}
		}

		*wids = mTmpWindows.data();
		return mTmpWindows.size();
	}

	bool getGroupTransform(PID pid, float* result)
	{
		auto itr = mWindowGroups.find(pid);
		if(itr == mWindowGroups.end()) { return false; }

		memcpy(result, itr->second.mTransform, sizeof(itr->second.mTransform));
		return true;
	}

	bool setGroupTransform(PID pid, const float* mtx)
	{
		auto itr = mWindowGroups.find(pid);
		if(itr == mWindowGroups.end()) { return false; }

		memcpy(itr->second.mTransform, mtx, sizeof(itr->second.mTransform));
		return true;
	}

	bool transformPoint(
		PID pid, unsigned int x, unsigned int y, float* out
	)
	{
		auto itr = mWindowGroups.find(pid);
		if(itr == mWindowGroups.end()) { return false; }

		float pos[] = {
			(float)x - (float)mHalfScreenWidth,
			-((float)y - (float)mHalfScreenHeight),
			0.f,
			1.f
		};
		float result[4];
		bx::vec4MulMtx(result, pos, itr->second.mTransform);
		out[0] = result[0];
		out[1] = result[1];
		out[2] = result[2];

		return true;
	}

	bool setFocusedWindow(WindowId window)
	{
		if(window == 0 || mWindows.find(window) != mWindows.end())
		{
			mFocusedWindow = window;
			return true;
		}
		else
		{
			return false;
		}
	}

	WindowId getFocusedWindow()
	{
		return mFocusedWindow;
	}

	const WindowInfo* getWindowInfo(WindowId window)
	{
		auto itr = mWindows.find(window);
		if(itr == mWindows.end()) { return NULL; }

		return &itr->second;
	}

private:
	int showHelp()
	{
		printf("Usage: xveearr --help\n");
		printf("       xveearr --version\n");
		printf("       xveearr [ --hmd <HMD> ]\n");
		printf("\n");
		printf("    --help                  Print this message\n");
		printf("    -v, --version           Show version info\n");
		printf("    -h, --hmd <HMD>         Choose HMD driver\n");

		return EXIT_SUCCESS;
	}

	int showVersion()
	{
		printf("xveearr version " XVEEARR_VERSION "\n");
		printf("\n");

		printf("Supported window systems:\n");
		printf("\n");
		for(IWindowSystem& winsys: Registry<IWindowSystem>::all())
		{
			printf("* %s\n", winsys.getName());
		}
		printf("\n");

		printf("Supported hmd drivers:\n");
		printf("\n");
		for(IHMD& hmd: Registry<IHMD>::all())
		{
			printf("* %s\n", hmd.getName());
		}
		printf("\n");

		printf("Supported controllers:\n");
		printf("\n");
		for(IController& controller: Registry<IController>::all())
		{
			printf("* %s\n", controller.getName());
		}

		return EXIT_SUCCESS;
	}

	bool init(int argc, const char* argv[])
	{
		bx::CommandLine cmdLine(argc, argv);
		const char* hmdName = cmdLine.findOption('h', "hmd", "null");

		for(IHMD& hmd: Registry<IHMD>::all())
		{
			if(strcmp(hmd.getName(),  hmdName) == 0)
			{
				mHMD = &hmd;
				break;
			}
		}

		if(mHMD == NULL)
		{
			return false;
		}

		if(!mHMD->init()) { return false; }

		if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { return false; }

		SDL_DisplayMode displayMode;
		memset(&displayMode, 0, sizeof(displayMode));
		SDL_GetCurrentDisplayMode(0, &displayMode);
		mHalfScreenWidth = displayMode.w / 2;
		mHalfScreenHeight = displayMode.h / 2;

		unsigned int viewportWidth, viewportHeight;
		mHMD->getViewportSize(viewportWidth, viewportHeight);

		mWindow = SDL_CreateWindow(
			"Xveearr",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			viewportWidth * 2, viewportHeight,
			SDL_WINDOW_SHOWN
		);

		if(!mWindow) { return false; }

		WindowSystemCfg wndSysCfg;
		wndSysCfg.mWindow = mWindow;
		for(IWindowSystem& winsys: Registry<IWindowSystem>::all())
		{
			if(winsys.init(wndSysCfg))
			{
				mWindowSystem = &winsys;
				break;
			}
			else
			{
				winsys.shutdown();
			}
		}

		if(!mWindowSystem) { return false; }

		ControllerCfg controllerCfg;
		controllerCfg.mWindow = mWindow;
		controllerCfg.mWindowManager = this;
		controllerCfg.mHMD = mHMD;
		for(IController& controller: Registry<IController>::all())
		{
			if(controller.init(controllerCfg))
			{
				mControllers.push_back(&controller);
			}
			else
			{
				controller.shutdown();
			}
		}

		mRenderThread.init(renderThread, this, 0, "Render thread");
		mRenderThreadReadySem.wait();

		bgfx::sdlSetWindow(mWindow);
		if(!bgfx::init()) { return false; }
		mBgfxInitialized = true;

		mHMD->prepareResources();

		bgfx::reset(viewportWidth * 2, viewportHeight, BGFX_RESET_VSYNC);
		bgfx::setDebug(BGFX_DEBUG_TEXT);

		const RenderData& leftEye = mHMD->getRenderData(Eye::Left);
		bgfx::setViewRect(
			RenderPass::LeftEye, 0, 0, viewportWidth, viewportHeight
		);
		bgfx::setViewFrameBuffer(RenderPass::LeftEye, leftEye.mFrameBuffer);
		bgfx::setViewClear(
			RenderPass::LeftEye, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH, gClearColor
		);

		const RenderData& rightEye = mHMD->getRenderData(Eye::Right);
		bgfx::setViewRect(
			RenderPass::RightEye, 0, 0, viewportWidth, viewportHeight
		);
		bgfx::setViewFrameBuffer(RenderPass::RightEye, rightEye.mFrameBuffer);
		bgfx::setViewClear(
			RenderPass::RightEye, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH, gClearColor
		);

		bgfx::setViewRect(
			RenderPass::Mirror, 0, 0, viewportWidth * 2, viewportHeight
		);
		bgfx::setViewClear(
			RenderPass::Mirror, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH, 0
		);

		float view[16];
		float proj[16];
		bx::mtxIdentity(view);
		bx::mtxOrthoRh(
			proj, 0, viewportWidth * 2, 0, viewportHeight, -1.0f, 1.0f
		);
		bgfx::setViewTransform(RenderPass::Mirror, view, proj);

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
		mQuadInfoUniform = bgfx::createUniform(
			"u_quadInfo", bgfx::UniformType::Vec4
		);

		return true;
	}

	void shutdown()
	{
		if(bgfx::isValid(mQuadInfoUniform)) { bgfx::destroyUniform(mQuadInfoUniform); }
		if(bgfx::isValid(mTextureUniform)) { bgfx::destroyUniform(mTextureUniform); }
		if(bgfx::isValid(mProgram)) { bgfx::destroyProgram(mProgram); }
		if(bgfx::isValid(mQuadIndices)) { bgfx::destroyIndexBuffer(mQuadIndices); }
		if(bgfx::isValid(mQuad)) { bgfx::destroyVertexBuffer(mQuad); }

		mHMD->releaseResources();

		if(mBgfxInitialized) { bgfx::shutdown(); }
		if(mRenderThread.isRunning()) { mRenderThread.shutdown(); }

		for(IController* controller: mControllers)
		{
			controller->shutdown();
		}

		if(mWindowSystem) { mWindowSystem->shutdown(); }

		if(mWindow) { SDL_DestroyWindow(mWindow); }
		SDL_Quit();

		if(mHMD) { mHMD->shutdown(); }
	}

	int mainLoop()
	{
		unsigned int viewportWidth, viewportHeight;
		mHMD->getViewportSize(viewportWidth, viewportHeight);

		float leftImageTransform[16];
		bx::mtxTranslate(leftImageTransform, 0, viewportHeight, 0.f);

		float rightImageTransform[16];
		bx::mtxTranslate(rightImageTransform, viewportWidth, viewportHeight, 0.f);

		while(true)
		{
			SDL_Event sdlEvent;
			while(SDL_PollEvent(&sdlEvent))
			{
				if(sdlEvent.type == SDL_QUIT) { return 0; }
			}

			WindowEvent windowEvent;
			while(mWindowSystem->pollEvent(windowEvent))
			{
				switch(windowEvent.mType)
				{
					case WindowEvent::WindowAdded:
						onWindowAdded(windowEvent);
						break;
					case WindowEvent::WindowRemoved:
						onWindowRemoved(windowEvent);
						break;
					case WindowEvent::WindowUpdated:
						onWindowUpdated(windowEvent);
						break;
				}
			}

			mHMD->update();

			for(IController* controller: mControllers)
			{
				controller->update();
			}

			const RenderData& leftEye = mHMD->getRenderData(Eye::Left);
			bgfx::setViewTransform(
				RenderPass::LeftEye,
				leftEye.mViewTransform,
				leftEye.mViewProjection
			);

			const RenderData& rightEye = mHMD->getRenderData(Eye::Right);
			bgfx::setViewTransform(
				RenderPass::RightEye,
				rightEye.mViewTransform,
				rightEye.mViewProjection
			);

			bgfx::touch(RenderPass::LeftEye);
			bgfx::touch(RenderPass::RightEye);
			for(auto&& pair: mWindowGroups)
			{
				const WindowGroup& group = pair.second;
				float zOrder = 0.f;
				bool focused = false;

				for(WindowId window: group.mMembers)
				{
					focused = window == mFocusedWindow;
					const WindowInfo& wndInfo = mWindows[window];

					float relTransform[16];
					bx::mtxTranslate(relTransform,
						(float)wndInfo.mX - (float)mHalfScreenWidth,
						-((float)wndInfo.mY - (float)mHalfScreenHeight),
						zOrder);
					float transform[16];
					bx::mtxMul(transform, relTransform, group.mTransform);

					bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
					loadTexturedQuad(
						transform,
						wndInfo.mTexture,
						wndInfo.mWidth, wndInfo.mHeight,
						wndInfo.mInvertedY
					);
					bgfx::submit(RenderPass::LeftEye, mProgram, 0, true);
					bgfx::submit(RenderPass::RightEye, mProgram, 0, false);

					++zOrder;
				}

				if(!focused) { continue; }

				CursorInfo cursorInfo = mWindowSystem->getCursorInfo();
				int mouseX, mouseY;
				SDL_GetGlobalMouseState(&mouseX, &mouseY);
				float cursorRelTransform[16];
				bx::mtxTranslate(cursorRelTransform,
					(float)mouseX - (float)cursorInfo.mOriginX - (float)mHalfScreenWidth,
					-((float)mouseY - (float)cursorInfo.mOriginY - (float)mHalfScreenHeight),
					zOrder);
				float cursorTransform[16];
				bx::mtxMul(cursorTransform, cursorRelTransform, group.mTransform);

				bgfx::setState(BGFX_STATE_DEFAULT | BGFX_STATE_BLEND_ALPHA);
				loadTexturedQuad(
					cursorTransform,
					cursorInfo.mTexture,
					cursorInfo.mWidth,
					cursorInfo.mHeight,
					true
				);
				bgfx::submit(RenderPass::LeftEye, mProgram, 0, true);
				bgfx::submit(RenderPass::RightEye, mProgram, 0, false);
			}

			loadTexturedQuad(
				leftImageTransform,
				leftEye.mFrameBuffer,
				viewportWidth, viewportHeight, false
			);
			bgfx::submit(RenderPass::Mirror, mProgram);

			loadTexturedQuad(
				rightImageTransform,
				rightEye.mFrameBuffer,
				viewportWidth, viewportHeight, false
			);
			bgfx::submit(RenderPass::Mirror, mProgram);

			bgfx::dbgTextClear();
			bgfx::dbgTextPrintf(0, 1, 0x4f, "Focused window: %zd", mFocusedWindow);

			bgfx::frame();
		}
	}

	template<typename T>
	void loadTexturedQuad(
		const float* transform,
		T texture,
		unsigned int width, unsigned int height,
		bool invertedY
	)
	{
		bgfx::setTransform(transform);
		bgfx::setVertexBuffer(mQuad);
		bgfx::setIndexBuffer(mQuadIndices);
		bgfx::setTexture(0, mTextureUniform, texture);
		float quadInfo[] = {
			(float)width,
			(float)height,
			invertedY ? 1.f : 0.f,
			0.f
		};
		bgfx::setUniform(mQuadInfoUniform, quadInfo, 1);
	}

	void onWindowAdded(const WindowEvent& event)
	{
		WindowGroup& group = findWindowGroup(event.mInfo.mPID);
		group.mMembers.push_back(event.mWindow);
		mWindows.insert(std::make_pair(event.mWindow, event.mInfo));
	}

	void onWindowRemoved(const WindowEvent& event)
	{
		WindowInfo wndInfo = mWindows[event.mWindow];
		mWindows.erase(event.mWindow);

		if(event.mWindow == mFocusedWindow) { mFocusedWindow = 0; }

		WindowGroup& group = findWindowGroup(wndInfo.mPID);
		group.mMembers.remove(event.mWindow);
		if(group.mMembers.empty())
		{
			mWindowGroups.erase(wndInfo.mPID);
			auto itr = std::find(mPIDs.begin(), mPIDs.end(), wndInfo.mPID);
			if(itr != mPIDs.end()) { mPIDs.erase(itr); }
		}
	}

	void onWindowUpdated(const WindowEvent& event)
	{
		mWindows[event.mWindow] = event.mInfo;
	}

	WindowGroup& findWindowGroup(uintptr_t pid)
	{
		auto itr = mWindowGroups.find(pid);
		if(itr == mWindowGroups.end())
		{
			WindowGroup group;
			float relTransform[16];
			bx::mtxTranslate(relTransform, 0.f, 0.f, -600.f);
			float headTransform[16];
			mHMD->getHeadTransform(headTransform);
			bx::mtxMul(group.mTransform, relTransform, headTransform);

			auto itr = mWindowGroups.insert(std::make_pair(pid, group));
			mPIDs.push_back(pid);

			return itr.first->second;
		}
		else
		{
			return itr->second;
		}
	}

	static int32_t renderThread(void* userData)
	{
		Application* app = static_cast<Application*>(userData);

		// Ensure that this thread is registered as the render thread before
		// bgfx is initialized
		bgfx::renderFrame();
		app->mHMD->initRenderer();
		app->mWindowSystem->initRenderer();
		app->mRenderThreadReadySem.post();

		while(true)
		{
			app->mHMD->beginRender();
			app->mWindowSystem->beginRender();
			bgfx::RenderFrame::Enum renderStatus = bgfx::renderFrame();
			app->mWindowSystem->endRender();
			app->mHMD->endRender();

			if(renderStatus == bgfx::RenderFrame::Exiting)
			{
				break;
			}
		}

		app->mWindowSystem->shutdownRenderer();
		app->mWindowSystem->shutdownRenderer();
		return 0;
	}

	IHMD* mHMD;
	SDL_Window* mWindow;
	bool mBgfxInitialized;
	IWindowSystem* mWindowSystem;
	unsigned int mHalfScreenWidth;
	unsigned int mHalfScreenHeight;
	bx::Thread mRenderThread;
	bx::Semaphore mRenderThreadReadySem;
	bgfx::VertexBufferHandle mQuad;
	bgfx::IndexBufferHandle mQuadIndices;
	bgfx::ProgramHandle mProgram;
	bgfx::UniformHandle mTextureUniform;
	bgfx::UniformHandle mQuadInfoUniform;
	WindowId mFocusedWindow;
	std::unordered_map<WindowId, WindowInfo> mWindows;
	std::unordered_map<PID, WindowGroup> mWindowGroups;
	std::vector<IController*> mControllers;
	std::vector<PID> mPIDs;
	std::vector<WindowId> mTmpWindows;
};

}

int main(int argc, const char* argv[])
{
	xveearr::Application app;
	return app.run(argc, argv);
}
