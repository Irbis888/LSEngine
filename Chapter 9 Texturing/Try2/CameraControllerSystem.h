#pragma once
#include <Commons.h>
#include "ResourceManager.h"
#include "World.h"


class CameraControllerSystem : public ISystem
{
public:
	void Update(entt::registry& reg, const FrameContext& context) override;

private:
	float mMoveSpeed = 20.0f;
	float mFastMultiplier = 4.0f;
	float mSlowMultiplier = 0.5f;
	float mMouseSensitivity = 0.007f;
	float mMinPitch = -1.5f;
	float mMaxPitch = 1.5f;
};

