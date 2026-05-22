#include "EditorContext.h"
#include "EditorSceneIO.h"

#include <Windows.h>
#include <imgui.h>
#include <ImGuizmo.h>

void Editor_DrawHierarchy(EditorContext& ctx);
void Editor_DrawInspector(EditorContext& ctx);
void Editor_DrawViewport(EditorContext& ctx);
void Editor_DrawStatistics(EditorContext& ctx, const FrameContext& context);
void Editor_DrawGizmoToolbar(EditorContext& ctx);
void Editor_DrawColliderBoundsDebug(EditorContext& ctx);
void Editor_DrawLegend(EditorContext& ctx);

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

    if (ImGui::BeginMenu("Debug"))
    {
        ImGui::MenuItem("Restore scene on Stop", nullptr, &ctx.restoreSceneOnStop);
        ImGui::SetItemTooltip(
            "When enabled, Stop reloads Scenes/EditorPlaySnapshot.json. "
            "When disabled, Stop only returns to Edit mode and keeps simulated changes.");

        if (ImGui::MenuItem("Restore Play Snapshot Now", EditorSceneIO::PlaySnapshotPath()))
            EditorSceneIO::RestorePlaySnapshot(ctx);
        ImGui::SetItemTooltip("Loads %s immediately (works in Play or Edit).", EditorSceneIO::PlaySnapshotPath());

        ImGui::Separator();
        ImGui::Text("Render bounding boxes");
        Editor_DrawColliderBoundsDebug(ctx);

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
    ImGui::Text("|");
    ImGui::SameLine();
    Editor_DrawColliderBoundsDebug(ctx);

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
