#pragma once

#include <entt/entt.hpp>
#include <ResourceManager.h>

#include <filesystem>
#include <string>
#include <vector>

struct EditorContext;

// Project-level editor actions: New/Open Project, scene files of the project and texture
// import. Native Windows dialogs are used for every file and folder choice.
namespace EditorProject
{
    // Native dialogs. Empty result when cancelled.
    std::filesystem::path PickFolder(const wchar_t* title, const std::filesystem::path& initial = {});
    std::filesystem::path PickOpenFile(const wchar_t* title, const std::filesystem::path& initial,
        const wchar_t* filterName, const wchar_t* filterPattern);
    std::filesystem::path PickSaveFile(const wchar_t* title, const std::filesystem::path& initial,
        const std::wstring& defaultName, const wchar_t* filterName, const wchar_t* filterPattern, const wchar_t* extension);

    // Restarts the editor process with another project (all engine caches belong to one
    // project, like the asset root of Lua, terrain and resource loading).
    bool RelaunchWithProject(EditorContext& ctx, const std::filesystem::path& projectFile);

    void OpenNewProjectDialog(EditorContext& ctx);
    void OpenProjectWithDialog(EditorContext& ctx);
    void DrawProjectDialogs(EditorContext& ctx);

    void NewScene(EditorContext& ctx);
    void OpenSceneWithDialog(EditorContext& ctx);
    void SaveSceneAsWithDialog(EditorContext& ctx);
    void SetCurrentSceneAsStartup(EditorContext& ctx);
    // Scene files of the active project (Scenes/** and Games/*/Scenes/**), project-relative.
    std::vector<std::string> ListScenes(EditorContext& ctx);

    // Returns the project-relative path of an image. Files outside the project are copied
    // into Assets/Textures first (imported), so the project stays self-contained.
    std::string ImportTexture(EditorContext& ctx, const std::filesystem::path& file);
    // Replaces the material of an entity's mesh and keeps the description saved with the scene.
    bool ApplyMaterial(EditorContext& ctx, entt::entity entity, const MaterialDesc& material);
}
