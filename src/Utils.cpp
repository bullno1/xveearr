#include "Utils.hpp"
#include <limits>
#include <bx/fpumath.h>

namespace xveearr
{
namespace utils
{

namespace
{

static const float gEpsilon = 0.000001f;

bool testRayVsTriangle(
	float* rayOrigin, float* rayDirection,
	float* v1, float* v2, float* v3,
	float& distance
)
{
	// Find vectors for two edges sharing V1
	float edge1[3];
	float edge2[3];
	bx::vec3Sub(edge1, v2, v1);
	bx::vec3Sub(edge2, v3, v1);

	// Begin calculating determinant - also used to calculate u parameter
	float p[3];
	bx::vec3Cross(p, rayDirection, edge2);

	// If determinant is near zero, ray lies in plane of triangle or
	// ray is parallel to plane of triangle
	float det = bx::vec3Dot(edge1, p);
	if(bx::fabsolute(det) < gEpsilon) { return false; }
	float invDet = 1.f / det;

	//calculate distance from v1 to ray origin
	float t[3];
	bx::vec3Sub(t, rayOrigin, v1);

	// Calculate u parameter and test bound
	float u = bx::vec3Dot(t, p) * invDet;
	//The intersection lies outside of the triangle
	if(u < 0.f || u > 1.f) { return false; }

	// Prepare to test v parameter
	float q[3];
	bx::vec3Cross(q, t, edge1);

	// Calculate V parameter and test bound
	float v = bx::vec3Dot(rayDirection, q) * invDet;
	// The intersection lies outside of the triangle
	if(v < 0.f || u + v  > 1.f) { return false; }

	distance = bx::vec3Dot(edge2, q) * invDet;
	return distance > gEpsilon;
}

bool testRayVsQuad(
	float* rayOrigin, float* rayDirection,
	float* topLeft, float* topRight, float* bottomRight, float* bottomLeft,
	float& distance
)
{
	bool hit = testRayVsTriangle(
		rayOrigin, rayDirection,
		topLeft, topRight, bottomRight,
		distance
	);
	if(hit) { return true; }

	hit = testRayVsTriangle(
		rayOrigin, rayDirection,
		topLeft, bottomRight, bottomLeft,
		distance
	);
	if(hit) { return true; }

	return false;
}

}

//https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
WindowId pickWindow(
	IWindowManager* windowManager,
	float* rayOrigin,
	float* rayDirection
)
{
	const WindowId* windows;
	unsigned int numWindows = windowManager->getWindows(0, &windows);

	float minDistance = std::numeric_limits<float>::max();
	WindowId window = 0;
	for(unsigned int i = 0; i < numWindows; ++i)
	{
		float groupTransform[16];
		const WindowInfo& wndInfo = *windowManager->getWindowInfo(windows[i]);
		windowManager->getGroupTransform(wndInfo.mPID, groupTransform);

		float topLeft[3];
		float topRight[3];
		float bottomRight[3];
		float bottomLeft[3];
		windowManager->transformPoint(
			wndInfo.mPID,
			wndInfo.mX, wndInfo.mY,
			topLeft
		);
		windowManager->transformPoint(
			wndInfo.mPID,
			wndInfo.mX + wndInfo.mWidth, wndInfo.mY,
			topRight
		);
		windowManager->transformPoint(
			wndInfo.mPID,
			wndInfo.mX + wndInfo.mWidth, wndInfo.mY + wndInfo.mHeight,
			bottomRight
		);
		windowManager->transformPoint(
			wndInfo.mPID,
			wndInfo.mX, wndInfo.mY + wndInfo.mHeight,
			bottomLeft
		);

		float distance;
		bool hit = testRayVsQuad(
			rayOrigin, rayDirection,
			topLeft, topRight, bottomRight, bottomLeft,
			distance
		);

		if(hit && distance < minDistance)
		{
			window = windows[i];
			minDistance = distance;
		}
	}

	return window;
}

}
}
