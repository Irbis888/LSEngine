#include "EditorContext.h"

#include <Engine.h>
#include <imgui.h>
#include <SceneFactory.h>

#include <string>

namespace
{
    int gSpawnCounter = 0;

    std::string NextSpawnName(const char* prefix)
    {
        return std::string(prefix) + "_" + std::to_string(++gSpawnCounter);
    }
}

void Editor_DrawSpawnPanel(EditorContext& ctx)
{
    if (!ctx.showSpawnWindow)
        return;

    if (!ImGui::Begin("Spawn Objects", &ctx.showSpawnWindow))
    {
        ImGui::End();
        return;
    }

    if (!ctx.engine)
    {
        ImGui::TextDisabled("Engine not bound.");
        ImGui::End();
        return;
    }

    entt::registry& registry = ctx.engine->GetRegistry();
    ResourceManager& resources = ctx.engine->GetResources();
    SceneFactory factory(registry, resources);

    ImGui::Text("Spawn location (world)");
    ImGui::DragFloat3("Position##spawn", &ctx.spawnPosition.x, 0.25f);
    ImGui::DragFloat("Uniform scale##spawn", &ctx.spawnUniformScale, 0.05f, 0.1f, 20.0f);

    const glm::vec3 scale(ctx.spawnUniformScale);

    ImGui::Separator();

    if (ImGui::Button("Spawn sphere (dynamic + physics)"))
    {
        const MaterialID material = resources.CreateSolidMaterial(
            NextSpawnName("SpawnSphereMat"),
            glm::vec3(0.15f, 0.55f, 0.85f),
            0.35f);

        const entt::entity entity = factory.CreatePrimitive(
            NextSpawnName("SpawnSphere"),
            PrimitiveType::Sphere,
            material,
            ctx.spawnPosition,
            glm::vec3(0.0f),
            scale);

        registry.emplace<RigidbodyComponent>(
            entity,
            RigidbodyComponent{ RigidbodyType::Dynamic, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f, true });
        registry.emplace<ColliderComponent>(
            entity,
            ColliderComponent{
                ColliderType::Sphere,
                glm::vec3(0.0f),
                glm::vec3(0.5f),
                0.5f,
                false,
                0.2f,
                0.4f });

        ctx.selected = entity;
        ctx.registry = &registry;
        ctx.statusMessage = "Spawned dynamic sphere at ("
            + std::to_string(ctx.spawnPosition.x) + ", "
            + std::to_string(ctx.spawnPosition.y) + ", "
            + std::to_string(ctx.spawnPosition.z) + ").";
    }

    if (ImGui::Button("Spawn box (brick texture, dynamic + physics)"))
    {
        Material material;
        material.name = NextSpawnName("SpawnBrickMat");
        material.albedo = resources.LoadTexture(L"bricks2.dds");
        material.normal = resources.LoadTexture(L"bricks2_nmap.dds");
        material.color = glm::vec3(1.0f);
        material.roughness = 0.3f;

        const MaterialID materialId = resources.CreateMaterial(material);
        const entt::entity entity = factory.CreatePrimitive(
            NextSpawnName("SpawnBrickBox"),
            PrimitiveType::Cube,
            materialId,
            ctx.spawnPosition,
            glm::vec3(0.0f),
            scale);

        registry.emplace<RigidbodyComponent>(
            entity,
            RigidbodyComponent{ RigidbodyType::Dynamic, glm::vec3(0.0f), glm::vec3(0.0f), 2.0f, true });
        registry.emplace<ColliderComponent>(
            entity,
            ColliderComponent{
                ColliderType::AABB,
                glm::vec3(0.0f),
                glm::vec3(0.5f),
                0.5f,
                false,
                0.05f,
                0.6f });

        ctx.selected = entity;
        ctx.registry = &registry;
        ctx.statusMessage = "Spawned brick box (textures: bricks2.dds).";
    }

    if (ImGui::Button("Spawn point light"))
    {
        const entt::entity entity = factory.CreateEmpty(
            NextSpawnName("SpawnPointLight"),
            ctx.spawnPosition);

        registry.emplace<PointLightComponent>(
            entity,
            PointLightComponent{
                glm::vec3(1.0f, 0.9f, 0.7f),
                2.5f,
                2.0f,
                30.0f,
                true });

        ctx.selected = entity;
        ctx.registry = &registry;
        ctx.statusMessage = "Spawned point light at spawn position.";
    }

    ImGui::Separator();
    ImGui::TextDisabled("Brick DDS paths resolve via ../../Textures/ (exe working dir).");

    ImGui::End();
}
