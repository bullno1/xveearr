#include <unordered_map>
#include <list>
#include <vector>
#include <algorithm>
#include <iterator>
#define SDL_MAIN_HANDLED
#include <SDL_syswm.h>
#include <SDL.h>
#include <bgfx/bgfxplatform.h>
#include <bgfx/bgfx.h>
#include <bx/bx.h>
#include <bx/platform.h>
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
#include "Log.hpp"

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

	int run(int argc, char* argv[])
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

		XVR_LOG(Info, "Initializing...");
		if(!init(argc, argv))
		{
			shutdown();
			return EXIT_FAILURE;
		}

		XVR_LOG(Info, "Initialization completed, entering main loop");
		int exitCode = mainLoop();
		XVR_LOG(Info, "Main loop terminated");
		shutdown();

		return exitCode;
	}

	unsigned int getWindowGroups(const PID** pids)
	{
		*pids = mPIDs.data();
		return (unsigned int)mPIDs.size();
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
		return (unsigned int)mTmpWindows.size();
	}

	bool getGroupTransform(PID pid, float* result)
	{
		auto itr = mWindowGroups.find(pid);
		XVR_ENSURE(itr != mWindowGroups.end(), "Invalid PID");

		memcpy(result, itr->second.mTransform, sizeof(itr->second.mTransform));
		return true;
	}

	bool setGroupTransform(PID pid, const float* mtx)
	{
		auto itr = mWindowGroups.find(pid);
		XVR_ENSURE(itr != mWindowGroups.end(), "Invalid PID");

		memcpy(itr->second.mTransform, mtx, sizeof(itr->second.mTransform));
		return true;
	}

	bool transformPoint(
		PID pid, unsigned int x, unsigned int y, float* out
	)
	{
		auto itr = mWindowGroups.find(pid);
		XVR_ENSURE(itr != mWindowGroups.end(), "Invalid PID");

		float pos[] = {
			(float)x * mXPixelsToMeters - mHalfScreenWidth,
			-((float)y * mYPixelsToMeters - mHalfScreenHeight),
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
			XVR_LOG(Warn, "Trying to set focus to a non-existent window");
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
		printf("       xveearr [ --log <Level> ] [ --hmd <HMD> ]\n");
		printf("\n");
		printf("    --help                  Print this message\n");
		printf("    -v, --version           Show version info\n");
		printf("    -h, --hmd <HMD>         Choose HMD driver\n");
		printf("    -l, --log <Level>       Set log level\n");

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

	bool init(int argc, char* argv[])
	{
		bx::CommandLine cmdLine(argc, argv);

		const char* logLevelStr = cmdLine.findOption('l', "log", "info");
		Log::Level logLevel = Log::parseLogLevel(logLevelStr);
		XVR_ENSURE(logLevel < Log::Count, "Invalid log level: ", logLevelStr);
		Log::setLogLevel(logLevel);

		const char* hmdName = cmdLine.findOption('h', "hmd", "null");

		XVR_LOG(Info, "Looking for HMD driver");
		for(IHMD& hmd: Registry<IHMD>::all())
		{
			if(strcmp(hmd.getName(), hmdName) == 0)
			{
				XVR_LOG(Info, "Use ", hmdName);
				mHMD = &hmd;
				break;
			}
		}

		XVR_ENSURE(mHMD != NULL, "Could not find HMD driver");
		XVR_ENSURE(mHMD->init(), "Could no initialize HMD");

		SDL_SetMainReady();
		XVR_ENSURE(
			SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0,
			"Could not initialize SDL"
		);

		unsigned int viewportWidth, viewportHeight;
		mHMD->getViewportSize(viewportWidth, viewportHeight);

		mWindow = SDL_CreateWindow(
			"Xveearr",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			viewportWidth * 2, viewportHeight,
			SDL_WINDOW_SHOWN
		);

		XVR_ENSURE(mWindow, "Could not create window");

		XVR_LOG(Info, "Looking for WindowSystem");
		WindowSystemCfg wndSysCfg;
		wndSysCfg.mWindow = mWindow;
		for(IWindowSystem& winsys: Registry<IWindowSystem>::all())
		{
			XVR_LOG(Info, "Trying ", winsys.getName());
			if(winsys.init(wndSysCfg))
			{
				XVR_LOG(Info, "Use ", winsys.getName());
				mWindowSystem = &winsys;
				break;
			}
			else
			{
				winsys.shutdown();
			}
		}

		XVR_ENSURE(mWindowSystem, "Could not find a suitable WindowSystem");

		XVR_LOG(Info, "Initializing Controller(s)");
		ControllerCfg controllerCfg;
		controllerCfg.mWindow = mWindow;
		controllerCfg.mWindowManager = this;
		controllerCfg.mHMD = mHMD;
		for(IController& controller: Registry<IController>::all())
		{
			XVR_LOG(Info, "Trying ", controller.getName());
			if(controller.init(controllerCfg))
			{
				XVR_LOG(Info, "Use ", controller.getName());
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
		XVR_ENSURE(bgfx::init(), "Could not initialize bgfx");
		mBgfxInitialized = true;

		mHMD->prepareResources();
		mHMD->update();

		bgfx::reset(viewportWidth * 2, viewportHeight, BGFX_RESET_VSYNC);
		bgfx::setDebug(BGFX_DEBUG_TEXT);

		DisplayMetrics displayMetrics = mWindowSystem->getDisplayMetrics();
		XVR_LOG(Debug, "Width in pixels = ", displayMetrics.mWidthInPixels);
		XVR_LOG(Debug, "Height in pixels = ", displayMetrics.mHeightInPixels);
		XVR_LOG(Debug, "Width in meters = ", displayMetrics.mWidthInMeters);
		XVR_LOG(Debug, "Height in meters = ", displayMetrics.mHeightInMeters);

		mHalfScreenWidth = displayMetrics.mWidthInMeters * 0.5f;
		mHalfScreenHeight = displayMetrics.mHeightInMeters * 0.5f;
		mXPixelsToMeters = displayMetrics.mWidthInMeters / (float)displayMetrics.mWidthInPixels;
		mYPixelsToMeters = displayMetrics.mHeightInMeters / (float)displayMetrics.mHeightInPixels;

		float hmdAspectRatio = (float)viewportWidth / (float)viewportHeight;
		float destkopAspectRatio = (float)displayMetrics.mWidthInMeters / (float)displayMetrics.mHeightInMeters;
		bool fitWidth = destkopAspectRatio > hmdAspectRatio;
		float halfFitDim = fitWidth ? mHalfScreenWidth : mHalfScreenHeight;

		const RenderData& eye = mHMD->getRenderData(Eye::Left);
		float wat = eye.mViewProjection[5];
		float watwat = fitWidth ? wat / hmdAspectRatio : wat;
		// how much of the view will the virtual desktop take
		float viewRatio = 0.8f;
		mPlacementDistance = halfFitDim / viewRatio * watwat;
		XVR_LOG(Debug, "Placment distance = ", mPlacementDistance);

		const RenderData& leftEye = mHMD->getRenderData(Eye::Left);
		bgfx::setViewRect(
			RenderPass::LeftEye, 0, 0,
			(uint16_t)viewportWidth, (uint16_t)viewportHeight
		);
		bgfx::setViewFrameBuffer(RenderPass::LeftEye, leftEye.mFrameBuffer);
		bgfx::setViewClear(
			RenderPass::LeftEye, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH, gClearColor
		);

		const RenderData& rightEye = mHMD->getRenderData(Eye::Right);
		bgfx::setViewRect(
			RenderPass::RightEye, 0, 0,
			(uint16_t)viewportWidth, (uint16_t)viewportHeight
		);
		bgfx::setViewFrameBuffer(RenderPass::RightEye, rightEye.mFrameBuffer);
		bgfx::setViewClear(
			RenderPass::RightEye, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH, gClearColor
		);

		bgfx::setViewRect(
			RenderPass::Mirror, 0, 0,
			(uint16_t)(viewportWidth * 2), (uint16_t)viewportHeight
		);
		bgfx::setViewClear(
			RenderPass::Mirror, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH, 0
		);

		float view[16];
		float proj[16];
		bx::mtxIdentity(view);
		bx::mtxOrthoRh(
			proj, 0, (float)viewportWidth * 2, 0, (float)viewportHeight, -1.0f, 1.0f
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
		XVR_LOG(Info, "Shutting down...");
		if(bgfx::isValid(mQuadInfoUniform)) { bgfx::destroyUniform(mQuadInfoUniform); }
		if(bgfx::isValid(mTextureUniform)) { bgfx::destroyUniform(mTextureUniform); }
		if(bgfx::isValid(mProgram)) { bgfx::destroyProgram(mProgram); }
		if(bgfx::isValid(mQuadIndices)) { bgfx::destroyIndexBuffer(mQuadIndices); }
		if(bgfx::isValid(mQuad)) { bgfx::destroyVertexBuffer(mQuad); }

		if(mBgfxInitialized)
		{
			mHMD->releaseResources();
			bgfx::shutdown();
		}

		if(mRenderThread.isRunning()) { mRenderThread.shutdown(); }

		for(IController* controller: mControllers)
		{
			controller->shutdown();
		}

		if(mWindowSystem) { mWindowSystem->shutdown(); }

		if(mWindow) { SDL_DestroyWindow(mWindow); }
		SDL_Quit();

		if(mHMD) { mHMD->shutdown(); }
		XVR_LOG(Info, "Shutdown completed");
	}

	int mainLoop()
	{
		unsigned int viewportWidth, viewportHeight;
		mHMD->getViewportSize(viewportWidth, viewportHeight);

		float leftImageTransform[16];
		bx::mtxTranslate(leftImageTransform, 0, (float)viewportHeight, 0.f);

		float rightImageTransform[16];
		bx::mtxTranslate(
			rightImageTransform, (float)viewportWidth, (float)viewportHeight, 0.f
		);

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
					focused |= window == mFocusedWindow;
					const WindowInfo& wndInfo = mWindows[window];

					float relTransform[16];
					bx::mtxTranslate(relTransform,
						(float)wndInfo.mX * mXPixelsToMeters - mHalfScreenWidth,
						-((float)wndInfo.mY * mYPixelsToMeters - mHalfScreenHeight),
						zOrder);
					float transform[16];
					bx::mtxMul(transform, relTransform, group.mTransform);

					bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
					loadTexturedQuad(
						transform,
						wndInfo.mTexture,
						wndInfo.mWidth * mXPixelsToMeters,
						wndInfo.mHeight * mYPixelsToMeters,
						wndInfo.mInvertedY
					);
					bgfx::submit(RenderPass::LeftEye, mProgram, 0, true);
					bgfx::submit(RenderPass::RightEye, mProgram, 0, false);

					zOrder += 0.0001f;
				}

				if(!focused) { continue; }

				CursorInfo cursorInfo = mWindowSystem->getCursorInfo();
				int mouseX, mouseY;
				SDL_GetGlobalMouseState(&mouseX, &mouseY);
				float cursorRelTransform[16];
				float cursorXInMeters = (mouseX - cursorInfo.mOriginX) * mXPixelsToMeters;
				float cursorYInMeters = (mouseY - cursorInfo.mOriginY) * mYPixelsToMeters;
				bx::mtxTranslate(cursorRelTransform,
					cursorXInMeters - mHalfScreenWidth,
					-(cursorYInMeters - mHalfScreenHeight),
					zOrder);
				float cursorTransform[16];
				bx::mtxMul(cursorTransform, cursorRelTransform, group.mTransform);

				bgfx::setState(BGFX_STATE_DEFAULT | BGFX_STATE_BLEND_ALPHA);
				loadTexturedQuad(
					cursorTransform,
					cursorInfo.mTexture,
					cursorInfo.mWidth * mXPixelsToMeters,
					cursorInfo.mHeight * mYPixelsToMeters,
					true
				);
				bgfx::submit(RenderPass::LeftEye, mProgram, 0, true);
				bgfx::submit(RenderPass::RightEye, mProgram, 0, false);
			}

			loadTexturedQuad(
				leftImageTransform,
				leftEye.mFrameBuffer,
				(float)viewportWidth, (float)viewportHeight, false
			);
			bgfx::submit(RenderPass::Mirror, mProgram);

			loadTexturedQuad(
				rightImageTransform,
				rightEye.mFrameBuffer,
				(float)viewportWidth, (float)viewportHeight, false
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
		float width, float height,
		bool invertedY
	)
	{
		bgfx::setTransform(transform);
		bgfx::setVertexBuffer(mQuad);
		bgfx::setIndexBuffer(mQuadIndices);
		bgfx::setTexture(0, mTextureUniform, texture);
		float quadInfo[] = { width, height, invertedY ? 1.f : 0.f, 0.f };
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
			bx::mtxTranslate(relTransform, 0.f, 0.f, -mPlacementDistance);
			float headTransform[16];
			mHMD->getHeadTransform(headTransform);
			bx::mtxMul(group.mTransform, relTransform, headTransform);

			auto itr2 = mWindowGroups.insert(std::make_pair(pid, group));
			mPIDs.push_back(pid);

			return itr2.first->second;
		}
		else
		{
			return itr->second;
		}
	}

	static int32_t renderThread(void* userData)
	{
		XVR_LOG(Info, "Render thread started");
		Application* app = static_cast<Application*>(userData);

		// Ensure that this thread is registered as the render thread before
		// bgfx is initialized
		XVR_LOG(Info, "Initializing render thread...");
		bgfx::renderFrame();
		app->mHMD->initRenderer();
		app->mWindowSystem->initRenderer();
		app->mRenderThreadReadySem.post();
		XVR_LOG(Info, "Initialization completed, entering render loop");

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

		XVR_LOG(Info, "Render loop terminated, shutting down...");
		app->mWindowSystem->shutdownRenderer();
		app->mWindowSystem->shutdownRenderer();
		XVR_LOG(Info, "Render thread terminated");
		return 0;
	}

	IHMD* mHMD;
	SDL_Window* mWindow;
	bool mBgfxInitialized;
	IWindowSystem* mWindowSystem;
	float mHalfScreenWidth;
	float mHalfScreenHeight;
	float mXPixelsToMeters;
	float mYPixelsToMeters;
	float mPlacementDistance;
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

int main(int argc, char* argv[])
{
	xveearr::Application app;
	return app.run(argc, argv);
}

#if BX_PLATFORM_WINDOWS != 0

int WINAPI WinMain(
	HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow
)
{
	BX_UNUSED(hInstance, hPrevInstance, lpCmdLine, nCmdShow);

	return main(__argc, __argv);
}

#endif