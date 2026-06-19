#pragma once
#include "Commons.h"
#include "ResourceManager.h"
#include "World.h"


class RenderSystem : public ISystem
{
public:
    RenderSystem(IRenderAdapter* adapter)
        : mAdapter(adapter) {
    }
    void Update(entt::registry& reg, const FrameContext& context) override
    {
        mAdapter->SetTimeData(context.timer.TotalTime(), context.timer.DeltaTime());
        reg.view<TransformComponent, CameraComponent>().each([this, &context](auto& transform, auto& camera)
            {
				mAdapter->SetCamera(camera, transform);
				
            });
        SceneLightData lights;
        reg.view<DirectionalLightComponent>().each([&lights](const DirectionalLightComponent& light)
            {
                if (!light.enabled || lights.directionalLights.size() >= RenderDirectionalLightCount)
                {
                    return;
                }

                lights.directionalLights.push_back(DirectionalLightData{
                    light.color * light.intensity,
                    glm::normalize(light.direction) });
            });
        reg.view<TransformComponent, PointLightComponent>().each([&lights](const TransformComponent& transform, const PointLightComponent& light)
            {
                if (!light.enabled || lights.pointLights.size() >= RenderPointLightCount)
                {
                    return;
                }

                lights.pointLights.push_back(PointLightData{
                    light.color * light.intensity,
                    transform.position,
                    light.falloffStart,
                    light.falloffEnd });
            });
        reg.view<TransformComponent, SpotLightComponent>().each([&lights](const TransformComponent& transform, const SpotLightComponent& light)
            {
                if (!light.enabled || lights.spotLights.size() >= RenderSpotLightCount)
                {
                    return;
                }

                lights.spotLights.push_back(SpotLightData{
                    light.color * light.intensity,
                    transform.position,
                    glm::normalize(light.direction),
                    light.falloffStart,
                    light.falloffEnd,
                    light.spotPower });
            });
        mAdapter->SetLights(lights);
		mAdapter->UpdCB();
        reg.view<TransformComponent, MeshComponent>().each([this, &context](auto& transform, auto& mesh)
            {
                mAdapter->SetTransform(transform);
				mAdapter->DrawMesh(mesh.meshID);
            });
    }
private:
    IRenderAdapter* mAdapter;
    ResourceManager* mResourceManager;
};

