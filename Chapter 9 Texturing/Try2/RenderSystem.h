#pragma once
#include "Commons.h"
#include "ResourceManager.h"
#include "World.h"
#include "TerrainSystem.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>


class RenderSystem : public ISystem
{
public:
    RenderSystem(IRenderAdapter* adapter, ResourceManager& resources, TerrainSystem* terrain = nullptr)
        : mAdapter(adapter), mTerrain(terrain), mResources(resources)
    {
    }
    void Update(entt::registry& reg, const FrameContext& context) override
    {
        mAdapter->SetTimeData(context.timer.TotalTime(), context.timer.DeltaTime());
        glm::vec3 eyePosition(0);
        glm::vec3 viewDirection(0, 0, 1);
        for (auto entity : reg.view<TransformComponent, CameraComponent>())
        {
            const auto& transform = reg.get<TransformComponent>(entity);
            eyePosition = transform.position;
            // The camera applies roll, then pitch, then yaw; roll leaves forward unchanged.
            viewDirection = glm::vec3(
                std::cos(transform.rotation.x) * std::sin(transform.rotation.y),
                -std::sin(transform.rotation.x),
                std::cos(transform.rotation.x) * std::cos(transform.rotation.y));
            mAdapter->SetCamera(reg.get<CameraComponent>(entity), transform);
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
        CollectEmissiveLights(reg, eyePosition, lights);
        mAdapter->SetLights(lights);
		mAdapter->UpdCB();
        SubmitMeshes(reg, eyePosition, viewDirection);
    }
private:
    IRenderAdapter* mAdapter;
    TerrainSystem* mTerrain;
    ResourceManager& mResources;

    struct Submission
    {
        entt::entity entity;
        const TransformComponent* transform;
        MeshID mesh;
        uint32_t submesh;
        MaterialID material;
        float depth;
        int layer;
        bool blended;
    };
    std::vector<Submission> mSubmissions;

    void SubmitMeshes(entt::registry& registry, const glm::vec3& eyePosition, const glm::vec3& viewDirection)
    {
        CollectSubmissions(registry, eyePosition, viewDirection);
        ZoneScopedN("Render submit (bind material + draw)");
        bool terrainDrawn = false;
        for (const auto& submission : mSubmissions)
        {
            if (submission.blended && !terrainDrawn)
            {
                if (mTerrain)
                {
                    mTerrain->Draw(registry);
                }
                terrainDrawn = true;
            }
            mAdapter->SetRenderEntity(submission.entity);
            mAdapter->SetTransform(*submission.transform);
            mAdapter->DrawSubmeshWithMaterial(submission.mesh, submission.submesh, submission.material);
        }
        if (!terrainDrawn && mTerrain)
        {
            mTerrain->Draw(registry);
        }
    }

    void CollectSubmissions(entt::registry& registry, const glm::vec3& eyePosition, const glm::vec3& viewDirection)
    {
        ZoneScopedN("Render collect and sort");
        mSubmissions.clear();
        for (auto entity : registry.view<TransformComponent, MeshComponent>())
        {
            const auto& transform = registry.get<TransformComponent>(entity);
            const MeshID meshID = registry.get<MeshComponent>(entity).meshID;
            const auto& mesh = mResources.GetMesh(meshID);
            const auto* overrideMaterial = registry.try_get<MaterialComponent>(entity);
            const auto* order = registry.try_get<RenderOrderComponent>(entity);
            const float depth = glm::dot(transform.position - eyePosition, viewDirection);
            for (uint32_t index = 0; index < mesh.submeshes.size(); ++index)
            {
                const MaterialID materialID = overrideMaterial && overrideMaterial->material
                    ? overrideMaterial->material : mesh.submeshes[index].material;
                const auto& material = mResources.GetMaterial(materialID);
                const bool blended = material.base && material.base->renderState.blendEnabled;
                mSubmissions.push_back({entity, &transform, meshID, index, materialID,
                    std::isfinite(depth) ? depth : 0.0f, order ? order->layer : 0, blended});
            }
        }
        std::sort(mSubmissions.begin(), mSubmissions.end(), [](const Submission& left, const Submission& right)
        {
            if (left.blended != right.blended)
            {
                return !left.blended;
            }
            if (left.layer != right.layer)
            {
                return left.layer < right.layer;
            }
            if (left.depth != right.depth)
            {
                return left.blended ? left.depth > right.depth : left.depth < right.depth;
            }
            if (left.entity != right.entity)
            {
                return entt::to_integral(left.entity) < entt::to_integral(right.entity);
            }
            return left.submesh < right.submesh;
        });
    }

    void CollectEmissiveLights(entt::registry& registry, const glm::vec3& eyePosition, SceneLightData& lights)
    {
        struct Candidate
        {
            float distance;
            PointLightData light;
        };
        std::array<Candidate, RenderEmissiveLightCount> nearest{};
        size_t count = 0;
        for (auto entity : registry.view<TransformComponent, MeshComponent>())
        {
            const auto& mesh = mResources.GetMesh(registry.get<MeshComponent>(entity).meshID);
            if ((mesh.state != ResourceState::Ready && !mesh.isPrimitive) || mesh.submeshes.empty())
            {
                continue;
            }

            const glm::vec3 position = registry.get<TransformComponent>(entity).position;
            PointLightData strongest;
            float peak = 0;
            const auto consider = [&](MaterialID material)
            {
                const auto& parameters = mResources.GetMaterial(material).parameters;
                if (!std::isfinite(parameters.emissiveLightRange) || parameters.emissiveLightRange <= 0 ||
                    !std::isfinite(parameters.emissiveIntensity) || parameters.emissiveIntensity <= 0)
                {
                    return;
                }
                const glm::vec3 color = glm::clamp(parameters.emissiveColor, 0.0f, 1.0f);
                const glm::vec3 strength = color * std::min(parameters.emissiveIntensity, 100.0f);
                const float brightness = std::max({strength.r, strength.g, strength.b});
                if (std::isfinite(brightness) && brightness > peak)
                {
                    peak = brightness;
                    strongest = {strength, position, 0, std::min(parameters.emissiveLightRange, 1000.0f)};
                }
            };
            if (const auto* component = registry.try_get<MaterialComponent>(entity); component && component->material)
            {
                consider(component->material);
            }
            else
            {
                for (const auto& submesh : mesh.submeshes)
                {
                    consider(submesh.material);
                }
            }
            if (peak <= 0)
            {
                continue;
            }
            const float distance = glm::distance(eyePosition, position) - strongest.falloffEnd;
            if (!std::isfinite(distance))
            {
                continue;
            }
            size_t index = 0;
            while (index < count && nearest[index].distance <= distance)
            {
                ++index;
            }
            if (index == nearest.size())
            {
                continue;
            }
            count = std::min(count + 1, nearest.size());
            for (size_t next = count - 1; next > index; --next)
            {
                nearest[next] = nearest[next - 1];
            }
            nearest[index] = {distance, strongest};
        }
        for (size_t index = 0; index < count; ++index)
        {
            lights.emissiveLights.push_back(nearest[index].light);
        }
    }
};

