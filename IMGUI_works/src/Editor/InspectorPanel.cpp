#include "EditorContext.h"

#include <imgui.h>
#include <Commons.h>
#include <cstring>

static void DrawTransformEditor(TransformComponent& t)
{
    ImGui::DragFloat3("Position", &t.position.x, 0.05f);
    ImGui::DragFloat3("Rotation", &t.rotation.x, 0.5f);
    ImGui::DragFloat3("Scale", &t.scale.x, 0.05f, 0.001f, 100.0f);
}

static void DrawDirectionalLightEditor(DirectionalLightComponent& light)
{
    ImGui::Checkbox("Enabled", &light.enabled);
    ImGui::ColorEdit3("Color", &light.color.x);
    ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100.0f);
    ImGui::DragFloat3("Direction", &light.direction.x, 0.05f);
}

static void DrawPointLightEditor(PointLightComponent& light)
{
    ImGui::Checkbox("Enabled", &light.enabled);
    ImGui::ColorEdit3("Color", &light.color.x);
    ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100.0f);
    ImGui::DragFloat("Falloff Start", &light.falloffStart, 0.1f, 0.0f, 1000.0f);
    ImGui::DragFloat("Falloff End", &light.falloffEnd, 0.1f, 0.0f, 1000.0f);
}

static void DrawSpotLightEditor(SpotLightComponent& light)
{
    ImGui::Checkbox("Enabled", &light.enabled);
    ImGui::ColorEdit3("Color", &light.color.x);
    ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100.0f);
    ImGui::DragFloat3("Direction", &light.direction.x, 0.05f);
    ImGui::DragFloat("Falloff Start", &light.falloffStart, 0.1f, 0.0f, 1000.0f);
    ImGui::DragFloat("Falloff End", &light.falloffEnd, 0.1f, 0.0f, 1000.0f);
    ImGui::DragFloat("Spot Power", &light.spotPower, 1.0f, 1.0f, 128.0f);
}

static void DrawInspectorPanel(EditorContext& ctx)
{
    if (!ImGui::Begin("Inspector — Component Properties", &ctx.showInspector))
    {
        ImGui::End();
        return;
    }

    if (!ctx.registry || ctx.selected == entt::null || !ctx.registry->valid(ctx.selected))
    {
        ImGui::TextWrapped("No selection. Choose an entity in Scene Hierarchy.");
        ImGui::End();
        return;
    }

    if (!Editor_CanEditComponents())
        ImGui::BeginDisabled();

    const auto entity = ctx.selected;

    if (ctx.registry->all_of<TagComponent>(entity))
    {
        if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& tag = ctx.registry->get<TagComponent>(entity);
            char buf[256] = {};
            strncpy_s(buf, tag.tag.c_str(), sizeof(buf) - 1);
            if (ImGui::InputText("Name", buf, sizeof(buf)))
                tag.tag = buf;
        }
    }

    if (ctx.registry->all_of<TransformComponent>(entity))
    {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            DrawTransformEditor(ctx.registry->get<TransformComponent>(entity));
    }

    if (ctx.registry->all_of<MeshComponent>(entity))
    {
        if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& mesh = ctx.registry->get<MeshComponent>(entity);
            int id = (int)mesh.meshID;
            ImGui::InputInt("Mesh ID", &id, 0, 0, ImGuiInputTextFlags_ReadOnly);
            ImGui::TextDisabled("Mesh picker: [PLACEHOLDER]");
        }
    }

    if (ctx.registry->all_of<DirectionalLightComponent>(entity))
    {
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
            DrawDirectionalLightEditor(ctx.registry->get<DirectionalLightComponent>(entity));
    }

    if (ctx.registry->all_of<PointLightComponent>(entity))
    {
        if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen))
            DrawPointLightEditor(ctx.registry->get<PointLightComponent>(entity));
    }

    if (ctx.registry->all_of<SpotLightComponent>(entity))
    {
        if (ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen))
            DrawSpotLightEditor(ctx.registry->get<SpotLightComponent>(entity));
    }

    if (ctx.registry->all_of<RigidbodyComponent>(entity))
    {
        if (ImGui::CollapsingHeader("Rigidbody", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& rb = ctx.registry->get<RigidbodyComponent>(entity);
            int type = (int)rb.type;
            ImGui::Combo("Type", &type, "Static\0Dynamic\0Kinematic\0\0");
            rb.type = (RigidbodyType)type;
            ImGui::Checkbox("Use Gravity", &rb.useGravity);
            ImGui::Text("Velocity: (%.2f, %.2f, %.2f)", rb.velocity.x, rb.velocity.y, rb.velocity.z);
        }
    }

    if (ctx.registry->all_of<ColliderComponent>(entity))
    {
        if (ImGui::CollapsingHeader("BoxCollider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& col = ctx.registry->get<ColliderComponent>(entity);
            ImGui::DragFloat3("Offset", &col.offset.x, 0.05f);
            ImGui::DragFloat3("Half Extents", &col.halfExtents.x, 0.05f, 0.01f, 50.0f);
            ImGui::DragFloat("Restitution", &col.restitution, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Friction", &col.friction, 0.01f, 0.0f, 1.0f);
        }
    }

    if (!Editor_CanEditComponents())
    {
        ImGui::EndDisabled();
        ImGui::TextDisabled("Editing disabled in Play mode.");
    }

    ImGui::End();
}

void Editor_DrawInspector(EditorContext& ctx)
{
    DrawInspectorPanel(ctx);
}
