#include "IController.hpp"
#include <bx/fpumath.h>
#include "Registry.hpp"
#include "IWindowManager.hpp"
#include "IHMD.hpp"
#include "Utils.hpp"

namespace xveearr
{

class HMDController: public IController
{
public:
	bool init(const ControllerCfg& cfg)
	{
		mWindowManager = cfg.mWindowManager;
		mHMD = cfg.mHMD;

		return true;
	}

	void shutdown()
	{
	}

	const char* getName() const
	{
		return "hmd";
	}

	void update()
	{
		float headTransform[16];
		mHMD->getHeadTransform(headTransform);
		float rayOrigin[] = {
			headTransform[12],
			headTransform[13],
			headTransform[14]
		};
		float rayRelDirection[] = { 0.f, 0.f, -1.f };
		float rayDirection[4];
		bx::vec4MulMtx(rayDirection, rayRelDirection, headTransform);
		WindowId window = utils::pickWindow(
			mWindowManager, rayOrigin, rayDirection
		);
		mWindowManager->focusWindow(window);
	}

private:
	IWindowManager* mWindowManager;
	IHMD* mHMD;
};

XVR_REGISTER(IController, HMDController)

}
