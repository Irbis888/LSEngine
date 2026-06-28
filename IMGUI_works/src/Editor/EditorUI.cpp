#include "EditorContext.h"
#include "EditorSceneIO.h"
#include <ResourceManager.h>
#include <SceneFactory.h>

#include <Windows.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cstring>
#include <string>

void Editor_DrawHierarchy(EditorContext& ctx);
void Editor_DrawInspector(EditorContext& ctx);
void Editor_DrawViewport(EditorContext& ctx);
void Editor_DrawStatistics(EditorContext& ctx, const FrameContext& context);
void Editor_DrawGizmoToolbar(EditorContext& ctx);
void Editor_DrawLegend(EditorContext& ctx);

namespace
{
    struct PrimitiveCreatorState
    {
        int type = static_cast<int>(PrimitiveType::Cube);
        char name[64] = "Cube";
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 rotation = glm::vec3(0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
        glm::vec3 color = glm::vec3(0.75f);
        float roughness = 0.5f;
    };

    PrimitiveCreatorState g_PrimitiveCreator;

    const char* PrimitiveTypeName(PrimitiveType type)
    {
        switch (type)
        {
        case PrimitiveType::Plane:  return "Plane";
        case PrimitiveType::Cube:   return "Cube";
        case PrimitiveType::Sphere: return "Sphere";
        default:                    return "Primitive";
        }
    }

    bool EntityNameExists(const entt::registry& registry, const std::string& name)
    {
        const auto view = registry.view<TagComponent>();
        for (const entt::entity entity : view)
        {
            if (view.get<TagComponent>(entity).tag == name)
                return true;
        }
        return false;
    }

    std::string MakeUniqueEntityName(const entt::registry& registry, const std::string& requestedName)
    {
        const std::string baseName = requestedName.empty() ? "Primitive" : requestedName;
        if (!EntityNameExists(registry, baseName))
            return baseName;

        for (int suffix = 2; ; ++suffix)
        {
            const std::string candidate = baseName + " " + std::to_string(suffix);
            if (!EntityNameExists(registry, candidate))
                return candidate;
        }
    }

    void ResetPrimitiveCreator(PrimitiveType type)
    {
        g_PrimitiveCreator = PrimitiveCreatorState{};
        g_PrimitiveCreator.type = static_cast<int>(type);
        strcpy_s(
            g_PrimitiveCreator.name,
            sizeof(g_PrimitiveCreator.name),
            PrimitiveTypeName(type));
    }

    void OpenPrimitiveCreator(EditorContext& ctx, PrimitiveType type)
    {
        ResetPrimitiveCreator(type);
        ctx.showPrimitiveCreator = true;
    }

    void DrawPrimitiveCreator(EditorContext& ctx)
    {
        if (!ctx.showPrimitiveCreator)
            return;

        if (!ImGui::Begin("Create Primitive", &ctx.showPrimitiveCreator, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::End();
            return;
        }

        if (!Editor_CanEditComponents())
        {
            ImGui::TextDisabled("Primitive creation is available in Edit mode only.");
            ImGui::End();
            return;
        }

        static const char* typeNames[] = { "Plane", "Cube", "Sphere" };
        if (ImGui::Combo("Type", &g_PrimitiveCreator.type, typeNames, IM_ARRAYSIZE(typeNames)))
        {
            const PrimitiveType type = static_cast<PrimitiveType>(g_PrimitiveCreator.type);
            strcpy_s(
                g_PrimitiveCreator.name,
                sizeof(g_PrimitiveCreator.name),
                PrimitiveTypeName(type));
        }

        ImGui::InputText("Name", g_PrimitiveCreator.name, sizeof(g_PrimitiveCreator.name));
        ImGui::SeparatorText("Transform");
        ImGui::DragFloat3("Position", &g_PrimitiveCreator.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &g_PrimitiveCreator.rotation.x, 0.01f);
        ImGui::DragFloat3("Scale", &g_PrimitiveCreator.scale.x, 0.05f, 0.001f, 100.0f);
        ImGui::SeparatorText("Material");
        ImGui::ColorEdit3("Color", &g_PrimitiveCreator.color.x);
        ImGui::SliderFloat("Roughness", &g_PrimitiveCreator.roughness, 0.0f, 1.0f);

        const bool canCreate = ctx.registry && ctx.resources;
        if (!canCreate)
            ImGui::BeginDisabled();

        if (ImGui::Button("Create", ImVec2(110.0f, 0.0f)))
        {
            const PrimitiveType type = static_cast<PrimitiveType>(g_PrimitiveCreator.type);
            const std::string entityName = MakeUniqueEntityName(*ctx.registry, g_PrimitiveCreator.name);
            const MaterialID material = ctx.resources->CreateSolidMaterial(
                entityName + " Material",
                g_PrimitiveCreator.color,
                g_PrimitiveCreator.roughness);

            SceneFactory factory(*ctx.registry, *ctx.resources);
            ctx.selected = factory.CreatePrimitive(
                entityName,
                type,
                material,
                g_PrimitiveCreator.position,
                g_PrimitiveCreator.rotation,
                g_PrimitiveCreator.scale);

            ctx.statusMessage = "Created " + std::string(PrimitiveTypeName(type)) + ": " + entityName;
            ctx.showPrimitiveCreator = false;
        }

        if (!canCreate)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)))
            ctx.showPrimitiveCreator = false;

        if (!canCreate)
            ImGui::TextDisabled("Registry or ResourceManager is not bound.");

        ImGui::End();
    }
}

static void DrawMainMenu(EditorContext& ctx)
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Save Scene As...", EditorSceneIO::DefaultSavePath()))
            EditorSceneIO::SaveScene(ctx);
        ImGui::SetItemTooltip("Writes %s", EditorSceneIO::DefaultSavePath());

        if (ImGui::MenuItem("Load Scene From...", EditorSceneIO::DefaultSavePath()))
            EditorSceneIO::LoadScene(ctx);
        ImGui::SetItemTooltip("Loads %s and replaces the current level.", EditorSceneIO::DefaultSavePath());
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            PostQuitMessage(0);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Scene Hierarchy", nullptr, &ctx.showHierarchy);
        ImGui::SetItemTooltip("List entities; click to select.");

        ImGui::MenuItem("Inspector", nullptr, &ctx.showInspector);
        ImGui::SetItemTooltip("Edit components of the selected entity.");

        ImGui::MenuItem("Viewport Info", nullptr, &ctx.showViewport);
        ImGui::SetItemTooltip("Mode and size hints; 3D scene draws behind panels.");

        ImGui::MenuItem("Statistics", nullptr, &ctx.showStatistics);
        ImGui::SetItemTooltip("FPS, frame time, entity counts, collisions.");

        ImGui::MenuItem("Legend / Help", nullptr, &ctx.showLegend);
        ImGui::SetItemTooltip("Controls, abbreviations, and file paths.");

        ImGui::MenuItem("ImGui Demo (debug)", nullptr, &ctx.showDemo);
        ImGui::SetItemTooltip("Official Dear ImGui demonstration window.");

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Scenes"))
    {
        const bool canSwitchScene = ctx.mode == EditorMode::Edit && ctx.engine;
        if (ImGui::MenuItem("Demo Scene", EditorSceneIO::DemoScenePath(), false, canSwitchScene))
            EditorSceneIO::LoadScene(ctx, EditorSceneIO::DemoScenePath());
        ImGui::SetItemTooltip("Load the original engine demonstration scene.");

        if (ImGui::MenuItem(
                "Character Showcase",
                EditorSceneIO::CharacterShowcasePath(),
                false,
                canSwitchScene))
        {
            EditorSceneIO::LoadScene(ctx, EditorSceneIO::CharacterShowcasePath());
        }
        ImGui::SetItemTooltip("Load the plane with three portraits and three Diablo models.");

        if (!canSwitchScene)
        {
            ImGui::Separator();
            ImGui::TextDisabled("Scene switching is available in Edit mode.");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Create"))
    {
        const bool canCreate = Editor_CanEditComponents() && ctx.registry && ctx.resources;
        if (ImGui::MenuItem("Cube", nullptr, false, canCreate))
            OpenPrimitiveCreator(ctx, PrimitiveType::Cube);
        if (ImGui::MenuItem("Sphere", nullptr, false, canCreate))
            OpenPrimitiveCreator(ctx, PrimitiveType::Sphere);
        if (ImGui::MenuItem("Plane", nullptr, false, canCreate))
            OpenPrimitiveCreator(ctx, PrimitiveType::Plane);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug"))
    {
        ImGui::MenuItem("Restore scene on Stop", nullptr, &ctx.restoreSceneOnStop);
        ImGui::SetItemTooltip(
            "When enabled, Stop reloads Scenes/EditorPlaySnapshot.json. "
            "When disabled, Stop only returns to Edit mode and keeps simulated changes.");

        if (ImGui::MenuItem("Restore Play Snapshot Now", EditorSceneIO::PlaySnapshotPath()))
            EditorSceneIO::RestorePlaySnapshot(ctx);
        ImGui::SetItemTooltip("Loads %s immediately (works in Play or Edit).", EditorSceneIO::PlaySnapshotPath());

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("Show Legend Window"))
            ctx.showLegend = true;
        ImGui::Separator();
        ImGui::TextDisabled("Toggle UI: ~ (VK_OEM_3, US keyboard)");
        ImGui::TextDisabled("Requires .imgui_on next to Try2.exe");
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

static void DrawToolbar(EditorContext& ctx)
{
    ImGui::Text("Mode:");
    ImGui::SameLine();
    if (ctx.mode == EditorMode::Edit)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Edit");
        ImGui::SameLine();
        if (ImGui::Button("Play##toolbar"))
            EditorSceneIO::BeginPlay(ctx);
        ImGui::SetItemTooltip("Save snapshot and run physics (Play mode).");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Play");
        ImGui::SameLine();
        if (ImGui::Button("Stop##toolbar"))
            EditorSceneIO::EndPlay(ctx);
        ImGui::SetItemTooltip(
            "Exit Play mode. Restores snapshot only if Debug > Restore scene on Stop is checked.");

        ImGui::SameLine();
        if (ImGui::Button("Restore Snapshot##toolbar"))
            EditorSceneIO::RestorePlaySnapshot(ctx);
        ImGui::SetItemTooltip("Reload %s now without leaving Play mode.", EditorSceneIO::PlaySnapshotPath());

        ImGui::SameLine();
        ImGui::Checkbox("Restore on Stop##debug", &ctx.restoreSceneOnStop);
        ImGui::SetItemTooltip("Debug: auto-restore snapshot when Stop is pressed.");
    }

    ImGui::SameLine();
    ImGui::Text("| Gizmo:");
    ImGui::SameLine();
    Editor_DrawGizmoToolbar(ctx);

    ImGui::SameLine();
    ImGui::TextDisabled("| Undo/Redo [PLACEHOLDER]");

    if (!ctx.statusMessage.empty())
    {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", ctx.statusMessage.c_str());
    }
}

void Editor_BeginFrame(const FrameContext& context)
{
    (void)context;
    EditorContext& ctx = *Editor_GetContext();
    ImGuizmo::BeginFrame();
    DrawMainMenu(ctx);
    DrawToolbar(ctx);
}

void Editor_RenderUI(const FrameContext& context)
{
    EditorContext& ctx = *Editor_GetContext();

    DrawPrimitiveCreator(ctx);
    Editor_DrawHierarchy(ctx);
    Editor_DrawInspector(ctx);
    Editor_DrawViewport(ctx);
    Editor_DrawStatistics(ctx, context);
    Editor_DrawLegend(ctx);

    if (ctx.showDemo)
        ImGui::ShowDemoWindow(&ctx.showDemo);

    ImGui::Begin("Material Editor (PLACEHOLDER)");
    ImGui::TextDisabled("[PLACEHOLDER] Material editing panel");
    ImGui::End();

    ImGui::Begin("Asset Browser (PLACEHOLDER)");
    ImGui::TextDisabled("[PLACEHOLDER] Drag-drop assets into scene");
    if (ctx.resources)
        ImGui::Text("ResourceManager bound.");
    ImGui::End();

    Editor_RenderGizmo(context);
}
