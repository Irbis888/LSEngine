#include "EditorContext.h"

#include <imgui.h>
#include <Commons.h>
#include <cstring>
#include <Engine.h>
#include <ScriptSystem.h>
#include <AssetPaths.h>
#include <cmath>
#include "TerrainPanel.h"
#include "EditorProject.h"
#include <Terrain.h>
#include <ResourceManager.h>
#include <SceneSerializer.h>

// Texture slot of a mesh material. Returns true when the path changed (Browse, Enter, Clear).
static bool DrawTexturePath(EditorContext& ctx, const char* label, std::wstring& texture)
{
    bool changed = false;
    ImGui::PushID(label);
    char text[4097]{};
    strncpy_s(text, AssetPaths::ToUtf8(texture).c_str(), sizeof(text) - 1);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::InputText(label, text, sizeof(text), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        texture = AssetPaths::FromUtf8(text).wstring();
        changed = true;
    }
    ImGui::SetItemTooltip("Project-relative image path (PNG, TGA, DDS...). Press Enter to apply.");
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        const auto file = EditorProject::PickOpenFile(L"Choose texture", ctx.engine->GetAssetRoot() / L"Assets" / L"Textures",
            L"Images (*.png;*.tga;*.dds;*.jpg;*.bmp)", L"*.png;*.tga;*.dds;*.jpg;*.jpeg;*.bmp");
        if (!file.empty())
        {
            try
            {
                texture = AssetPaths::FromUtf8(EditorProject::ImportTexture(ctx, file)).wstring();
                changed = true;
            }
            catch (const std::exception& error) { ctx.statusMessage = std::string("Texture import failed: ") + error.what(); }
        }
    }
    ImGui::SetItemTooltip("Files outside the project are copied into Assets/Textures.");
    ImGui::SameLine();
    if (ImGui::Button("Clear") && !texture.empty()) { texture.clear(); changed = true; }
    ImGui::PopID();
    return changed;
}

static void DrawMaterialEditor(EditorContext& ctx, entt::entity entity)
{
    auto* mesh = ctx.registry->try_get<MeshComponent>(entity);
    auto* source = ctx.registry->try_get<MeshSourceComponent>(entity);
    if (!mesh || !source || !ctx.resources || !ctx.engine) return;
    if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& stored = source->mesh.material;
    MaterialDesc material = stored ? *stored : MaterialDesc{};
    if (!stored)
    {
        const auto* tag = ctx.registry->try_get<TagComponent>(entity);
        material.name = (tag ? tag->tag : std::string("Entity")) + " Material";
        ImGui::TextDisabled("Model materials are used. Changes override every submesh.");
    }
    bool changed = false;
    ImGui::ColorEdit3("Color", &material.color.x);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    changed |= DrawTexturePath(ctx, "Albedo", material.albedoTexture);
    changed |= DrawTexturePath(ctx, "Normal", material.normalTexture);
    if (stored && !changed)
    {
        // Keep the live drag preview in the description; the GPU material updates on release.
        stored->color = material.color;
        stored->roughness = material.roughness;
    }
    if (changed) EditorProject::ApplyMaterial(ctx, entity, material);
}

static void DrawScriptEditor(EditorContext& ctx, entt::entity entity)
{
    if (!ctx.engine) return;
    auto& registry = *ctx.registry;
    const bool canEdit = Editor_CanEditComponents();
    auto* component = registry.try_get<ScriptComponent>(entity);
    if (!component)
    {
        ImGui::BeginDisabled(!canEdit);
        if (ImGui::Button("Add Lua Script")) registry.emplace<ScriptComponent>(entity);
        ImGui::EndDisabled();
        return;
    }
    if (!ImGui::CollapsingHeader("Lua Script", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& scripts = ctx.engine->GetScripts();
    ImGui::TextWrapped("State: %s", scripts.GetStatus(entity).c_str());
    const auto runtimeError = scripts.GetError(entity);
    if (!runtimeError.empty()) ImGui::TextWrapped("%s", runtimeError.c_str());
    ImGui::BeginDisabled(ctx.engine->IsSceneLoading());
    ImGui::Checkbox("Script enabled", &component->enabled);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Disabling pauses callbacks/events and preserves the instance. Init is not repeated on re-enable.");
    ImGui::BeginDisabled(!canEdit);
    char path[4097]{};
    strncpy_s(path, component->path.c_str(), sizeof(path) - 1);
    if (ImGui::InputText("Lua file", path, sizeof(path))) component->path = path;
    ImGui::SetItemTooltip("UTF-8 path relative to the asset root, for example Scripts/Rotate.lua. Reload is Edit-only.");
    bool reload = ImGui::Button("Reload description");
    ImGui::SameLine();
    if (ImGui::Button("Reset parameter overrides")) component->parameters.clear();
    ImGui::EndDisabled();
    if (!canEdit) ImGui::TextDisabled("File, parameters and Reload are editable only outside Play.");
    if (!component->path.empty())
    {
        try
        {
            if (reload) component->path = AssetPaths::ScriptPath(ctx.engine->GetAssetRoot(), component->path);
            const auto& description = scripts.Describe(component->path, reload);
            if (!description.error.empty()) ImGui::TextWrapped("%s", description.error.c_str());
            ImGui::BeginDisabled(!canEdit);
            for (const auto& [name, defaultValue] : description.defaults)
            {
                ImGui::PushID(name.c_str());
                const auto existing = component->parameters.find(name);
                auto value = existing == component->parameters.end() ? defaultValue : existing->second;
                if (value.index() != defaultValue.index())
                {
                    ImGui::TextWrapped("%s: incompatible type, expected %s. Reset overrides.", name.c_str(), ScriptValueType(defaultValue));
                    ImGui::PopID();
                    continue;
                }
                bool changed = false;
                if (auto* v = std::get_if<bool>(&value)) changed = ImGui::Checkbox(name.c_str(), v);
                else if (auto* v = std::get_if<double>(&value))
                { changed = ImGui::InputDouble(name.c_str(), v, 0.1, 1, "%.5f"); if (!std::isfinite(*v)) changed = false; }
                else if (auto* v = std::get_if<glm::vec3>(&value))
                { changed = ImGui::DragFloat3(name.c_str(), &v->x, 0.01f); }
                else
                {
                    char text[4097]{};
                    strncpy_s(text, std::get<std::string>(value).c_str(), sizeof(text) - 1);
                    changed = ImGui::InputText(name.c_str(), text, sizeof(text));
                    if (changed) value = std::string(text);
                }
                if (changed) component->parameters[name] = std::move(value);
                ImGui::PopID();
            }
            for (const auto& [name, value] : component->parameters)
                if (!description.defaults.contains(name)) ImGui::TextWrapped("Unknown parameter: %s. Reset overrides.", name.c_str());
            ImGui::EndDisabled();
        }
        catch (const std::exception& error) { ImGui::TextWrapped("%s", error.what()); }
    }
    ImGui::BeginDisabled(!canEdit);
    if (ImGui::Button("Remove ScriptComponent")) registry.remove<ScriptComponent>(entity);
    ImGui::EndDisabled();
}

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
        {
            auto& t=ctx.registry->get<TransformComponent>(entity);
            if(ctx.registry->all_of<TerrainComponent>(entity))
            {
                ImGui::DragFloat3("Position",&t.position.x,.1f);
                ImGui::DragFloat3("Scale",&t.scale.x,.01f,.001f,100);
                ImGui::TextDisabled("Terrain rotation is unsupported.");
                if(t.rotation!=glm::vec3(0)&&ImGui::Button("Reset terrain rotation"))t.rotation={0,0,0};
            }
            else DrawTransformEditor(t);
        }
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
        DrawMaterialEditor(ctx, entity);
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
        if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& col = ctx.registry->get<ColliderComponent>(entity);
            int shape = static_cast<int>(col.type);
            if (ImGui::Combo("Shape", &shape, "Box (AABB)\0Sphere\0\0"))
                col.type = static_cast<ColliderType>(shape);
            ImGui::DragFloat3("Offset", &col.offset.x, 0.05f);
            if (col.type == ColliderType::Sphere)
            {
                ImGui::DragFloat("Radius", &col.radius, 0.05f, 0.01f, 50.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetItemTooltip("Local radius; world radius uses the largest absolute scale component.");
            }
            else
            {
                ImGui::DragFloat3("Half Extents", &col.halfExtents.x, 0.05f, 0.01f, 50.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetItemTooltip("Axis-aligned box. Transform rotation does not rotate this collider.");
            }
            ImGui::DragFloat("Restitution", &col.restitution, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Friction", &col.friction, 0.01f, 0.0f, 1.0f);
        }
    }

    Editor_DrawTerrainInspector(ctx,entity);
    if (!Editor_CanEditComponents())
    {
        ImGui::EndDisabled();
        ImGui::TextDisabled("Editing disabled in Play mode.");
    }

    DrawScriptEditor(ctx, entity);
    ImGui::End();
}

void Editor_DrawInspector(EditorContext& ctx)
{
    DrawInspectorPanel(ctx);
}
