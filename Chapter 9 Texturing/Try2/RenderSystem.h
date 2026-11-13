#pragma once
#include "Commons.h"
#include "ResourceManager.h"
#include "World.h"
#include "TerrainSystem.h"
#include <glm/gtc/matrix_transform.hpp>


class RenderSystem : public ISystem
{
public:
    RenderSystem(IRenderAdapter* adapter, TerrainSystem* terrain = nullptr)
        : mAdapter(adapter), mTerrain(terrain) {
    }
    void Update(entt::registry& reg, const FrameContext& context) override
    {
        mAdapter->SetTimeData(context.timer.TotalTime(), context.timer.DeltaTime());
        for (auto entity : reg.view<TransformComponent, CameraComponent>())
        {
            mAdapter->SetCamera(reg.get<CameraComponent>(entity), reg.get<TransformComponent>(entity));
            break;
        }
        SceneLightData lights;
        reg.view<TransformComponent, DirectionalLightComponent>().each([&lights](const TransformComponent& transform, const DirectionalLightComponent& light)
            {
                if (!light.enabled || lights.directionalLights.size() >= RenderDirectionalLightCount)
                {
                    return;
                }

                const glm::vec3 direction = LightDirectionFromRotation(transform.rotation);
                lights.directionalLights.push_back(DirectionalLightData{
                    light.color * light.intensity,
                    direction });
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
                    LightDirectionFromRotation(transform.rotation),
                    light.falloffStart,
                    light.falloffEnd,
                    light.spotPower });
            });
        mAdapter->SetLights(lights);
		mAdapter->UpdCB();
        reg.view<TransformComponent, MeshComponent>().each([this, &context](entt::entity entity, auto& transform, auto& mesh)
            {
                mAdapter->SetRenderEntity(entity);
                mAdapter->SetTransform(transform);
				mAdapter->DrawMesh(mesh.meshID);
            });
        if(mTerrain)mTerrain->Draw(reg);
    }
private:
    IRenderAdapter* mAdapter;
    TerrainSystem* mTerrain;
    ResourceManager* mResourceManager;
};

