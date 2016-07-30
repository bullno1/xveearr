#include <iostream>
#include <SDL.h>
#include <bx/timer.h>
#include "Application.hpp"

using namespace xveearr;

int main(int argc, const char* argv[])
{
	(void)argc;
	(void)argv;

	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { return 1; }

	SDL_Window* window = SDL_CreateWindow(
		"Xveearr",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		1280, 720,
		SDL_WINDOW_SHOWN
	);

	if(window == NULL) { return 1; }

	Application& app = Application::getInstance();
	ApplicationContext appCtx;
	appCtx.mArgc = argc;
	appCtx.mArgv = argv;
	appCtx.mWindow = window;
	bool running = app.init(appCtx);

	double timerFreq = bx::getHPFrequency();
	double lastTime = (double)bx::getHPCounter() / timerFreq;
	double accumulator = 0;
	double deltaTime = 1.0 / 60.0;

	while(running)
	{
		SDL_Event ev;
		while(SDL_PollEvent(&ev))
		{
			running &= app.onEvent(ev);
		}

		double now = (double)bx::getHPCounter() / timerFreq;
		double frameTime = now - lastTime;
		lastTime = now;
		accumulator += frameTime;
		while(running && accumulator >= deltaTime)
		{
			running &= app.update();
			accumulator -= deltaTime;
		}

		app.render();
	}

	app.shutdown();
	SDL_DestroyWindow(window);

	SDL_Quit();
	return 0;
}
