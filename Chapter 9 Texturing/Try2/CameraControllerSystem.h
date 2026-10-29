#pragma once
#include <Commons.h>
#include "ResourceManager.h"
#include "World.h"
#include <functional>


class CameraControllerSystem : public ISystem
{
public:
	// While isPlaying() is true, the free camera moves only with the left mouse
	// button held, so gameplay keys (WASD) reach the scripts alone.
	explicit CameraControllerSystem(std::function<bool()> isPlaying = {}) : mIsPlaying(std::move(isPlaying)) {}
	void Update(entt::registry& reg, const FrameContext& context) override;

private:
	std::function<bool()> mIsPlaying;
	float mMoveSpeed = 20.0f;
	float mFastMultiplier = 4.0f;
	float mSlowMultiplier = 0.5f;
	float mMouseSensitivity = 0.007f;
	float mMinPitch = -1.5f;
	float mMaxPitch = 1.5f;
};

