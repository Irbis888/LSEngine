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
        DrawBullet("Create a primitive",
            "Use Create > Cube/Sphere/Plane or the + Create Primitive button in Scene Hierarchy. "
            "Choose its transform and material, then press Create.");
    }

    if (ImGui::CollapsingHeader("Edit vs Play", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawBullet("Edit mode",
            "Physics is OFF. You can move objects with the gizmo or Inspector fields. "
            "The editor always stays in Edit mode while a game window runs.");
        DrawBullet("Play (toolbar)",
            "Runs the edited scene in a separate game window (Try2.exe --play): own input, camera, "
            "physics and Lua. The current scene, saved or not, goes to Temp/Play/<Scene>.json in the project.");
        DrawBullet("Stop / closing the game window / Esc in it",
            "Ends Play. The editor scene was never changed, so nothing has to be restored.");
        DrawBullet("Debug > Simulate in Editor",
            "Old in-process Play on the editor scene (Inspector locked). Stop restores "
            "Scenes/EditorPlaySnapshot.json; Restore Snapshot reloads it immediately.");
    }

    if (ImGui::CollapsingHeader("Windows (View menu)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawBullet("Scene Hierarchy",
            "List of all entities in the level. Click to select.");
        DrawBullet("Inspector",
            "Edit components of the selected entity (Transform, lights, physics, etc.).");
        DrawBullet("Viewport",
            "Embedded 3D scene. Click a surface to select; drag LMB to look, WASD/QE to move while the viewport has focus.");
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
        ImGui::TextWrapped("Paths are relative to the project folder (the folder of the .lsproject file).");
        DrawBullet("New Project / Open Project",
            "Create a project anywhere on disk or open a folder with a .lsproject file; the editor restarts with it.");
        DrawBullet("New / Open / Save / Save As Scene",
            "Scene files inside the project (native dialogs). Ctrl+S saves the current scene.");
        DrawBullet("Set as Startup Scene",
            "Stores the current scene as startupScene in the project file.");
        DrawBullet("Exit",
            "Closes the application.");
        DrawBullet("Scenes menu",
            "Every scene file of the project; switching is available in Edit mode.");
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
            row("Edit", "Editor mode — design time, no physics.");
            row("Play", "Game in a separate window; physics and scripts run there.");
            row("Simulate", "Debug: in-process Play on the editor scene.");
            row("Restore Snapshot", "Manual reload of EditorPlaySnapshot.json (Simulate).");
            row(".lsproject", "Project file: name, format version, startup scene.");
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
