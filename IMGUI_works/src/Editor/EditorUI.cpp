#include "EditorContext.h"
#include "EditorLayout.h"
#include "EditorSceneIO.h"
#include "EditorProject.h"
#include "EditorPlayWindow.h"
#include <AssetPaths.h>
#include <ResourceManager.h>
#include <Engine.h>
#include <SceneFactory.h>
#include <SceneSerializer.h>
#include <ScriptSystem.h>

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
void Editor_DrawAssetsAndConsole(EditorContext& ctx);

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

            // Keep the description so File > Save writes a mesh that loads after a restart.
            SceneSerializer::MeshDescription source;
            source.source = "primitive";
            source.primitive = type == PrimitiveType::Plane ? "plane" : type == PrimitiveType::Sphere ? "sphere" : "cube";
            source.material = MaterialDesc{ entityName + " Material", {}, {}, g_PrimitiveCreator.color, g_PrimitiveCreator.roughness };
            ctx.registry->emplace<MeshSourceComponent>(ctx.selected, MeshSourceComponent{ std::move(source) });
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

    const bool canSwitchScene = Editor_CanEditComponents() && ctx.engine;
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Project..."))
            EditorProject::OpenNewProjectDialog(ctx);
        ImGui::SetItemTooltip("Create a project folder anywhere on disk and open it.");
        if (ImGui::MenuItem("Open Project..."))
            EditorProject::OpenProjectWithDialog(ctx);
        ImGui::SetItemTooltip("Choose a folder with a .lsproject file. The editor restarts with it.");
        ImGui::Separator();

        const std::string currentScene = ctx.engine ? ctx.engine->GetScenePath() : std::string();
        if (ImGui::MenuItem("New Scene...", nullptr, false, canSwitchScene))
            EditorProject::NewScene(ctx);
        ImGui::SetItemTooltip("Create a scene with a camera, a sun and a ground plane in the project.");
        if (ImGui::MenuItem("Open Scene...", nullptr, false, canSwitchScene))
            EditorProject::OpenSceneWithDialog(ctx);
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, ctx.mode == EditorMode::Edit && !currentScene.empty()))
            EditorSceneIO::SaveCurrentScene(ctx);
        ImGui::SetItemTooltip("Overwrites %s with the edited scene (Edit mode only).",
            currentScene.empty() ? "the loaded scene file" : currentScene.c_str());
        if (ImGui::MenuItem("Save Scene As...", nullptr, false, ctx.mode == EditorMode::Edit && ctx.engine))
            EditorProject::SaveSceneAsWithDialog(ctx);
        ImGui::SetItemTooltip("Write the scene to a new file inside the project; later saves go there.");
        const bool hasProject = ctx.engine && !ctx.engine->GetProject().file.empty();
        if (ImGui::MenuItem("Set as Startup Scene", nullptr, false, hasProject && !currentScene.empty()))
            EditorProject::SetCurrentSceneAsStartup(ctx);
        ImGui::SetItemTooltip("Store the current scene as startupScene in the .lsproject file.");
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

        ImGui::MenuItem("Viewport", nullptr, &ctx.showViewport);
        ImGui::SetItemTooltip("Scene render texture and entity selection.");

        ImGui::MenuItem("Statistics", nullptr, &ctx.showStatistics);
        ImGui::SetItemTooltip("FPS, frame time, entity counts, collisions.");

        ImGui::MenuItem("Legend / Help", nullptr, &ctx.showLegend);
        ImGui::SetItemTooltip("Controls, abbreviations, and file paths.");

        ImGui::MenuItem("ImGui Demo (debug)", nullptr, &ctx.showDemo);
        ImGui::SetItemTooltip("Official Dear ImGui demonstration window.");

        ImGui::MenuItem("Asset Browser", nullptr, &ctx.showAssets);
        ImGui::MenuItem("Console", nullptr, &ctx.showConsole);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
        {
            EditorLayout::Reset(ctx);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Scenes"))
    {
        // Every scene file of the project; the sample project lists the engine demos.
        const auto scenes = EditorProject::ListScenes(ctx);
        const std::string current = ctx.engine ? ctx.engine->GetScenePath() : std::string();
        for (const auto& scene : scenes)
        {
            if (ImGui::MenuItem(scene.c_str(), nullptr, scene == current, canSwitchScene))
                EditorSceneIO::LoadScene(ctx, scene.c_str());
            if (scene.find("LuaDemo") != std::string::npos)
                ImGui::SetItemTooltip("Play: arrows move, J/L control body, Space jumps, O opens door, X deletes purple cube.");
            else if (scene.find("BallField") != std::string::npos)
                ImGui::SetItemTooltip("Lua football: rules in Games/BallField/Scripts/FootballMatch.lua, ball/confetti from Prefabs/.");
            else if (scene.find("PhysicsStress10000") != std::string::npos)
                ImGui::SetItemTooltip("10,000 falling cubes above a large floor. Press Play to run physics.");
        }
        if (scenes.empty()) ImGui::TextDisabled("No scenes in Scenes/ yet.");

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
        if (ImGui::MenuItem("Simulate in Editor", nullptr, false, ctx.mode == EditorMode::Edit && !EditorPlayWindow::IsRunning()))
            EditorSceneIO::BeginPlay(ctx);
        ImGui::SetItemTooltip("Old in-process Play: scripts and physics run on the editor scene, Stop restores %s.",
            EditorSceneIO::PlaySnapshotPath());
        ImGui::TextDisabled("Stop always restores the simulation snapshot.");

        if (ImGui::MenuItem("Restore Simulation Snapshot Now", EditorSceneIO::PlaySnapshotPath()))
            EditorSceneIO::RestorePlaySnapshot(ctx);
        ImGui::SetItemTooltip("Loads %s in the background (works in Play or Edit).", EditorSceneIO::PlaySnapshotPath());

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
    if (ctx.engine && ctx.engine->GetSceneLoadProgress().path.find("LuaDemo") != std::string::npos)
        ImGui::TextWrapped("Lua demo: Arrows = move | J/L = body velocity | Space = impulse | O = door event | X = delete. Click the viewport before using keys.");
    if (ctx.engine && ctx.engine->GetSceneLoadProgress().path.find("BallField") != std::string::npos)
        ImGui::TextWrapped("Ball Field: Blue = WASD + Shift | Red = arrows + Enter | R = kickoff. In the game window the free camera moves only with the left mouse button held.");
    if (ctx.engine)
    {
        ImGui::TextDisabled("Project: %s", ctx.engine->GetProject().name.c_str());
        ImGui::SetItemTooltip("%s", AssetPaths::ToUtf8(ctx.engine->GetProject().file.empty() ?
            ctx.engine->GetAssetRoot() : ctx.engine->GetProject().file).c_str());
        ImGui::SameLine();
    }
    ImGui::Text("Mode:");
    ImGui::SameLine();
    if (ctx.mode == EditorMode::Play)
    {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Simulate (in editor)");
        ImGui::SameLine();
        if (ImGui::Button("Stop##toolbar"))
            EditorSceneIO::EndPlay(ctx);
        ImGui::SetItemTooltip(
            "Stop scripts and physics, call OnDestroy and restore the simulation snapshot.");

        ImGui::SameLine();
        if (ImGui::Button("Restore Snapshot##toolbar"))
            EditorSceneIO::RestorePlaySnapshot(ctx);
        ImGui::SetItemTooltip("Stop and restore %s.", EditorSceneIO::PlaySnapshotPath());
    }
    else if (EditorPlayWindow::IsRunning())
    {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Edit");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "| Playing in a separate window");
        ImGui::SameLine();
        if (ImGui::Button("Stop##toolbar"))
            EditorPlayWindow::Stop(ctx);
        ImGui::SetItemTooltip("Close the game window. The editor scene was never changed by Play.");
        ImGui::SameLine();
        if (ImGui::Button("Show Game##toolbar"))
            EditorPlayWindow::Focus();
        ImGui::SetItemTooltip("Bring the game window to the front (pid %lu).", EditorPlayWindow::ProcessId());
    }
    else
    {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Edit");
        ImGui::SameLine();
        const bool canPlay = ctx.engine && !ctx.engine->IsSceneLoading();
        ImGui::BeginDisabled(!canPlay);
        if (ImGui::Button("Play##toolbar"))
            EditorPlayWindow::Start(ctx);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Run the edited scene in a separate game window. "
            "Stop or closing that window returns here; the editor scene stays as it is.");
    }
    if (ctx.engine)
    {
        auto& scripts = ctx.engine->GetScripts();
        bool hotReload = scripts.IsHotReloadEnabled();
        ImGui::SameLine();
        if (ImGui::Checkbox("Hot reload##toolbar", &hotReload)) scripts.SetHotReload(hotReload);
        ImGui::SetItemTooltip("Watch Lua scripts and prefabs on a JobSystem worker. In Play, a saved script restarts its instances (reset state); a saved prefab applies to the next spawn.");
    }

    ImGui::SameLine();
    ImGui::Text("| Gizmo:");
    ImGui::SameLine();
    Editor_DrawGizmoToolbar(ctx);



    if (ctx.engine)
    {
        const auto& scene = ctx.engine->GetSceneLoadProgress();
        if (scene.active)
        {
            if (scene.total) ImGui::Text("Scene objects: %zu / %zu", scene.created, scene.total);
            else ImGui::TextUnformatted("Reading scene...");
        }
        else if (!scene.error.empty())
        {
            ImGui::TextWrapped("Scene load failed: %s", scene.error.c_str());
        }
        if (ctx.resources)
        {
            const auto textures = ctx.resources->GetTextureProgress();
            if (textures.total)
            {
                    ImGui::Text("Textures: %zu / %zu", textures.ready, textures.total);
                if (textures.failed) { ImGui::SameLine(); ImGui::Text("(%zu failed)", textures.failed); }
            }
        }
    }
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
    auto& viewportState = ctx.viewport;
    if (ctx.engine && (viewportState.sceneGeneration != ctx.engine->GetSceneGeneration() ||
        (ctx.selected != entt::null && !ctx.registry->valid(ctx.selected))))
    {
        Editor_SelectEntity(ctx, entt::null);
        viewportState.sceneGeneration = ctx.engine->GetSceneGeneration();
    }
    if (viewportState.observedSelection != ctx.selected)
    {
        Editor_SelectEntity(ctx, ctx.selected);
    }
    ViewportPickResult pickResult;
    while (ctx.engine && ctx.engine->GetRenderer().PollViewportPick(pickResult))
    {
        if (!ctx.engine->IsSceneLoading() && pickResult.request == viewportState.pendingPick &&
            pickResult.sceneGeneration == ctx.engine->GetSceneGeneration() &&
            (pickResult.entity == entt::null || ctx.registry->valid(pickResult.entity)))
        {
            Editor_SelectEntity(ctx, pickResult.entity);
        }
    }
    static bool wasLoading = false;
    const bool loading = ctx.engine && ctx.engine->IsSceneLoading();
    if (loading || wasLoading) ctx.selected = entt::null;
    if (wasLoading && !loading)
    {
        const auto& scene = ctx.engine->GetSceneLoadProgress();
        ctx.statusMessage = scene.error.empty() ? "Loaded from " + scene.path : "Load failed: " + scene.error;
    }
    wasLoading = loading;
    if (ctx.engine)
        if (auto notice = ctx.engine->GetScripts().TakeHotReloadNotice(); !notice.empty()) ctx.statusMessage = std::move(notice);
    EditorPlayWindow::Update(ctx);
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && ctx.mode == EditorMode::Edit &&
        ctx.engine && !ctx.engine->GetScenePath().empty())
        EditorSceneIO::SaveCurrentScene(ctx);
    ImGuizmo::BeginFrame();
    DrawMainMenu(ctx);
    const auto* viewport = ImGui::GetMainViewport();
    const float toolbarTop = viewport->WorkPos.y;
    const float toolbarHeight = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().WindowPadding.y * 2;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, toolbarTop));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, toolbarHeight));
    ImGui::Begin("Toolbar###EditorToolbar", nullptr, ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    DrawToolbar(ctx);
    ImGui::End();
    EditorLayout::DrawDockspace(ctx, toolbarTop + toolbarHeight);
}

void Editor_RenderUI(const FrameContext& context)
{
    EditorContext& ctx = *Editor_GetContext();

    DrawPrimitiveCreator(ctx);
    EditorProject::DrawProjectDialogs(ctx);
    if (ctx.showHierarchy)
    {
        Editor_DrawHierarchy(ctx);
    }
    if (ctx.showInspector)
    {
        Editor_DrawInspector(ctx);
    }
    Editor_DrawViewport(ctx);
    if (ctx.showStatistics)
    {
        Editor_DrawStatistics(ctx, context);
    }
    if (ctx.showLegend)
    {
        Editor_DrawLegend(ctx);
    }

    if (ctx.showDemo)
        ImGui::ShowDemoWindow(&ctx.showDemo);

    Editor_DrawAssetsAndConsole(ctx);
}
