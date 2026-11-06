#pragma once
#include <filesystem>
#include <string>
#include <vector>

// A game project: a folder outside the engine sources with a <Name>.lsproject file (JSON)
// and the standard layout Scenes/, Assets/, Scripts/, Prefabs/. Scene, script, prefab,
// model and texture paths are stored relative to the project folder. See PROJECTS.md.
struct ProjectInfo
{
    std::filesystem::path file; // Absolute path of the .lsproject file.
    std::filesystem::path root; // Folder of the .lsproject file: the asset root.
    std::string name;
    int formatVersion = 1;
    std::string startupScene; // Relative to root; may be empty.
    // Extra folders (relative to root) searched for textures referenced by bare file names,
    // e.g. "bricks.dds" from a scene or a model's .mtl file. The project root is always first.
    std::vector<std::string> textureSearchPaths;
};

namespace Project
{
    inline constexpr int FormatVersion = 1;
    inline constexpr const wchar_t* Extension = L".lsproject";
    inline constexpr const char* DefaultScene = "Scenes/Main.json";
    // Standard folders created for every new project.
    inline constexpr const char* Folders[] = { "Scenes", "Assets", "Assets/Textures", "Assets/Models", "Scripts", "Prefabs" };

    // Creates directory (when missing) with the standard folders, <name>.lsproject and a
    // starter scene. Fails if the folder already contains a project file.
    ProjectInfo Create(const std::filesystem::path& directory, const std::string& name);
    // Accepts a .lsproject file or a folder that contains exactly one .lsproject file.
    ProjectInfo Open(const std::filesystem::path& fileOrDirectory);
    // Project file inside directory; empty when there is none. Throws when ambiguous.
    std::filesystem::path FindFile(const std::filesystem::path& directory);
    void Save(const ProjectInfo& project);
    // The demo scenes shipped with the engine are an ordinary project in the engine folder.
    ProjectInfo OpenSample();

    // The project used by the editor/player in this process (sets AssetPaths' project root).
    void SetActive(const ProjectInfo& project);
    const ProjectInfo* Active();

    // Writes an empty scene with a camera, a sun and a ground plane.
    void WriteStarterScene(const std::filesystem::path& file);
    // Valid project names are also valid file names.
    bool IsValidName(const std::string& name);
}
