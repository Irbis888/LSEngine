#include "CameraControllerSystem.h"
#include "ImGuiBridge.h"

#include <cmath>

void CameraControllerSystem::Update(entt::registry& reg, const FrameContext& context)
{
	if (ImGuiBridge::WantsCaptureInput())
		return;

	const InputState& input = context.input;
	const float dt = context.timer.DeltaTime();
	const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

	reg.view<TransformComponent, CameraComponent>().each(
		[&](TransformComponent& transform, CameraComponent&)
		{
			if (input.mouseButtons[0])
			{
				transform.rotation.y += input.mouseDeltaX * mMouseSensitivity;
				transform.rotation.x += input.mouseDeltaY * mMouseSensitivity;

				if (transform.rotation.x < mMinPitch)
					transform.rotation.x = mMinPitch;
				else if (transform.rotation.x > mMaxPitch)
					transform.rotation.x = mMaxPitch;
			}

			const float pitch = transform.rotation.x;
			const float yaw = transform.rotation.y;

			glm::vec3 forward(
				cosf(pitch) * sinf(yaw),
				-sinf(pitch),
				cosf(pitch) * cosf(yaw));

			if (glm::length(forward) > 0.0001f)
				forward = glm::normalize(forward);

			glm::vec3 right = glm::cross(worldUp, forward);
			if (glm::length(right) > 0.0001f)
				right = glm::normalize(right);

			glm::vec3 movement(0.0f);

			if (input.keys['W'])
				movement += forward;
			if (input.keys['S'])
				movement -= forward;
			if (input.keys['D'])
				movement += right;
			if (input.keys['A'])
				movement -= right;
			if (input.keys['E'])
				movement += worldUp;
			if (input.keys['Q'])
				movement -= worldUp;

			if (glm::length(movement) > 0.0001f)
			{
				movement = glm::normalize(movement);

				float speed = mMoveSpeed;
				if (input.keys[VK_SHIFT])
					speed *= mFastMultiplier;
				if (input.keys[VK_CONTROL])
					speed *= mSlowMultiplier;

				transform.position += movement * speed * dt;
			}
		});
}
