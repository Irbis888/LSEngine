#include "EditorContext.h"

#include <imgui.h>
#include "EditorSceneIO.h"

static void DrawBullet(const char* title, const char* body)
{
    ImGui::BulletText("%s", title);
    if (body && body[0])
    {
        ImGui::Indent();
        ImGui::TextWrapped("%s", body);
        ImGui::Unindent();
    }
}

static void DrawLegendPanel(EditorContext& ctx)
{
    if (!ImGui::Begin("Legend / Help", &ctx.showLegend))
    {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Quick start", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawBullet("Enable editor",
            "Create an empty file named .imgui_on in the same folder as Try2.exe "
            "(for example x64/Debug/.imgui_on). Without it, the editor does not load.");
        DrawBullet("Show or hide UI",
            "Press ~ (tilde) on a US keyboard layout. Key code: VK_OEM_3. "
            "Toggle again to hide the overlay.");
        DrawBullet("Select an object",
            "Open Scene Hierarchy, click a row. Its components appear in Inspector.");
    }

    if (ImGui::CollapsingHeader("Edit vs Play", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawBullet("Edit mode (green in Viewport)",
            "Physics is OFF. You can move objects with the gizmo or Inspector fields. "
            "Use toolbar: Play.");
        DrawBullet("Play mode (yellow in Viewport)",
            "Physics is ON (same as running the game). Inspector editing is disabled.");
        DrawBullet("Play snapshot",
            "Pressing Play saves Scenes/EditorPlaySnapshot.json.");
        DrawBullet("Stop",
            "Returns to Edit mode. Restores snapshot only if Debug > Restore scene on Stop "
            "is checked (toolbar checkbox in Play mode).");
        DrawBullet("Restore Snapshot (button)",
            "Toolbar or Debug menu — reloads snapshot immediately; can use while still in Play.");
        DrawBullet("Restore scene on Stop (Debug)",
            "Checkbox in Debug menu or Play toolbar. Default OFF — enable to auto-restore on Stop.");
    }

    if (ImGui::CollapsingHeader("Windows (View menu)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawBullet("Scene Hierarchy",
            "List of all entities in the level. Click to select.");
        DrawBullet("Inspector",
            "Edit components of the selected entity (Transform, lights, physics, etc.).");
        DrawBullet("Viewport",
            "Information about the 3D view. The scene draws full-window behind ImGui panels.");
        DrawBullet("Statistics",
            "FPS, frame time, entity counts, physics collision count for this frame.");
        DrawBullet("Legend / Help",
            "This window — controls, abbreviations, and file paths.");
        DrawBullet("ImGui Demo",
            "Official Dear ImGui widget gallery (debug / learning only).");
    }

    if (ImGui::CollapsingHeader("Toolbar & gizmo", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Gizmo buttons (Edit mode, entity selected):");
        if (ImGui::BeginTable("gizmo_legend", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Short");
            ImGui::TableSetupColumn("Meaning");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Translate (T)");
            ImGui::TableNextColumn(); ImGui::TextWrapped("Move along axes (arrows).");
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Rotate (R)");
            ImGui::TableNextColumn(); ImGui::TextWrapped("Rotate around rings.");
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Scale (S)");
            ImGui::TableNextColumn(); ImGui::TextWrapped("Resize along axes.");
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("Local space");
            ImGui::TableNextColumn(); ImGui::TextWrapped("Gizmo aligned to object. Off = world axes.");
            ImGui::EndTable();
        }
        DrawBullet("Using the gizmo",
            "Click and drag arrows/rings in the 3D view. "
            "Mouse is only captured when the cursor is over an ImGui window that uses it.");
    }

    if (ImGui::CollapsingHeader("File menu", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Paths are relative to the working directory (often the exe folder when launched from VS).");
        DrawBullet("Save Scene",
            "Writes to Scenes/EditorSave.json via SceneSerializer.");
        DrawBullet("Load Scene",
            "Clears ECS and loads Scenes/EditorSave.json. Unsaved edits are lost.");
        DrawBullet("Exit",
            "Closes the application.");
    }

    if (ImGui::CollapsingHeader("Abbreviations & terms", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginTable("terms", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 220)))
        {
            ImGui::TableSetupColumn("Term");
            ImGui::TableSetupColumn("Meaning");
            ImGui::TableHeadersRow();

            auto row = [](const char* term, const char* meaning)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(term);
                ImGui::TableNextColumn(); ImGui::TextWrapped("%s", meaning);
            };

            row("ECS", "Entity Component System — entities with attached data (Transform, Mesh, …).");
            row("entt", "EnTT library — registry storing entities and components.");
            row("FPS", "Frames per second.");
            row("FPS (1s avg)", "Average FPS over the last ~1 second of frames.");
            row("FPS (instant)", "FPS from the current frame only.");
            row("RT", "Render target — GPU texture the scene is drawn into.");
            row("WYSIWYG", "What You See Is What You Get — edits show immediately in the 3D view.");
            row("DX12", "Direct3D 12 graphics API used by Try2.");
            row("SRV", "Shader Resource View — GPU descriptor for textures.");
            row("AABB", "Axis-aligned bounding box — physics collision shape.");
            row("RB", "Rigidbody — physics motion component.");
            row("Tag", "Display name for an entity in Hierarchy.");
            row("Mesh ID", "Index into ResourceManager meshes.");
            row("VK_OEM_3", "Virtual-key code for ~ on US keyboards.");
            row("PLACEHOLDER", "Feature planned but not implemented yet.");
            row("Edit", "Editor mode — design time, no physics.");
            row("Play", "Run mode — physics and simulation active.");
            row("Restore on Stop", "Debug option — reload snapshot when Stop is pressed.");
            row("Collider bounds", "Debug/gizmo: Off, All, or Selected wireframe AABBs.");
            row("Restore Snapshot", "Manual reload of EditorPlaySnapshot.json.");
            row("Licensed", "Internal term: .imgui_on file present; editor allowed to run.");

            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Inspector fields (short)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginTable("insp", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Field");
            ImGui::TableSetupColumn("Description");
            ImGui::TableHeadersRow();
            auto row = [](const char* a, const char* b) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(a);
                ImGui::TableNextColumn(); ImGui::TextWrapped("%s", b);
            };
            row("Position", "World position (x, y, z).");
            row("Rotation", "Euler angles in radians (pitch, yaw, roll order in engine).");
            row("Scale", "Non-uniform scale per axis.");
            row("Use Gravity", "Dynamic bodies accelerate downward when enabled.");
            row("Falloff Start/End", "Distance range for light attenuation.");
            row("Spot Power", "Cone sharpness for spot lights.");
            row("Half Extents", "Half-size of box collider.");
            row("Restitution", "Bounciness (0 = none, 1 = full).");
            row("Friction", "Surface friction for collisions.");
            ImGui::EndTable();
        }
    }

    if (!ctx.statusMessage.empty())
    {
        ImGui::Separator();
        ImGui::Text("Last status:");
        ImGui::TextWrapped("%s", ctx.statusMessage.c_str());
    }

    ImGui::End();
}

void Editor_DrawLegend(EditorContext& ctx)
{
    DrawLegendPanel(ctx);
}
