#include "EditorContext.h"

#include <imgui.h>
#include <Commons.h>
#include <unordered_set>
#include <string>
#include <vector>

static std::string EntityLabel(entt::registry& reg, entt::entity entity)
{
    if (reg.all_of<TagComponent>(entity))
    {
        const auto& tag = reg.get<TagComponent>(entity);
        if (!tag.tag.empty())
            return tag.tag;
    }

    if (reg.all_of<CameraComponent>(entity))
        return "Camera";
    if (reg.all_of<DirectionalLightComponent>(entity))
        return "Directional Light";
    if (reg.all_of<PointLightComponent>(entity))
        return "Point Light";
    if (reg.all_of<SpotLightComponent>(entity))
        return "Spot Light";

    return "Entity " + std::to_string((uint32_t)entity);
}

static void CollectEntities(entt::registry& reg, std::vector<entt::entity>& out)
{
    std::unordered_set<entt::entity> seen;

    auto addView = [&](auto view)
    {
        for (auto entity : view)
        {
            if (reg.valid(entity))
                seen.insert(entity);
        }
    };

    addView(reg.view<TagComponent>());
    addView(reg.view<TransformComponent>());
    addView(reg.view<MeshComponent>());
    addView(reg.view<CameraComponent>());
    addView(reg.view<DirectionalLightComponent>());
    addView(reg.view<PointLightComponent>());
    addView(reg.view<SpotLightComponent>());
    addView(reg.view<RigidbodyComponent>());
    addView(reg.view<ColliderComponent>());

    out.assign(seen.begin(), seen.end());
}

static void DrawHierarchyPanel(EditorContext& ctx)
{
    if (!ImGui::Begin("Scene Hierarchy", &ctx.showHierarchy))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Click a row to select. Parent/child tree: [PLACEHOLDER]");

    if (!ctx.registry)
    {
        ImGui::Text("No registry bound.");
        ImGui::End();
        return;
    }

    std::vector<entt::entity> entities;
    CollectEntities(*ctx.registry, entities);

    ImGui::Text("Entities: %zu", entities.size());

    for (auto entity : entities)
    {
        const bool selected = ctx.selected == entity;
        const std::string label = EntityLabel(*ctx.registry, entity);

        if (ImGui::Selectable(label.c_str(), selected))
            ctx.selected = entity;
    }

    ImGui::End();
}

void Editor_DrawHierarchy(EditorContext& ctx)
{
    DrawHierarchyPanel(ctx);
}
