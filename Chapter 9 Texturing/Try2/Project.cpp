#include "Project.h"
#include "AssetPaths.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <optional>
#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
    std::optional<ProjectInfo> g_Active;

    json ReadJson(const fs::path& file)
    {
        std::ifstream in(file);
        if (!in) throw std::runtime_error("Cannot open project file: " + AssetPaths::ToUtf8(file));
        try { return json::parse(in); }
        catch (const json::exception& error)
        {
            throw std::runtime_error("Project file " + AssetPaths::ToUtf8(file) + " is not valid JSON: " + error.what());
        }
    }

    void WriteJson(const fs::path& file, const json& value)
    {
        // Write next to the target first so a failed write never leaves a truncated project file.
        const auto temporary = fs::path(file).concat(L".tmp");
        {
            std::ofstream out(temporary, std::ios::trunc);
            if (!out) throw std::runtime_error("Cannot write " + AssetPaths::ToUtf8(file));
            out << value.dump(2) << '\n';
            if (!out) throw std::runtime_error("Cannot write " + AssetPaths::ToUtf8(file));
        }
        fs::rename(temporary, file);
    }
}

bool Project::IsValidName(const std::string& name)
{
    if (name.empty() || name.size() > 100 || name.back() == '.' || name.back() == ' ' || name.front() == ' ')
        return false;
    for (const unsigned char c : name)
        if (c < 32 || std::string_view("<>:\"/\\|?*").find(char(c)) != std::string_view::npos) return false;
    return true;
}

fs::path Project::FindFile(const fs::path& directory)
{
    if (!fs::is_directory(directory)) return {};
    fs::path found;
    for (const auto& entry : fs::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || _wcsicmp(entry.path().extension().c_str(), Extension) != 0) continue;
        if (!found.empty())
            throw std::runtime_error("Folder " + AssetPaths::ToUtf8(directory) + " contains several .lsproject files");
        found = entry.path();
    }
    return found;
}

ProjectInfo Project::Open(const fs::path& fileOrDirectory)
{
    fs::path file = fs::weakly_canonical(fs::absolute(fileOrDirectory));
    if (fs::is_directory(file))
    {
        const auto inside = FindFile(file);
        if (inside.empty()) throw std::runtime_error("No .lsproject file in " + AssetPaths::ToUtf8(file));
        file = inside;
    }
    if (!fs::is_regular_file(file)) throw std::runtime_error("Project not found: " + AssetPaths::ToUtf8(file));
    if (_wcsicmp(file.extension().c_str(), Extension) != 0)
        throw std::runtime_error("Expected a .lsproject file: " + AssetPaths::ToUtf8(file));

    const json data = ReadJson(file);
    if (!data.is_object()) throw std::runtime_error("Project file must contain a JSON object");
    ProjectInfo project;
    project.file = file;
    project.root = file.parent_path();
    project.formatVersion = data.value("formatVersion", 0);
    if (project.formatVersion < 1 || project.formatVersion > FormatVersion)
        throw std::runtime_error("Unsupported project format version " + std::to_string(project.formatVersion) +
            " (this engine reads version " + std::to_string(FormatVersion) + ")");
    project.name = data.value("name", AssetPaths::ToUtf8(file.stem()));
    project.startupScene = data.value("startupScene", std::string());
    if (data.contains("textureSearchPaths"))
        for (const auto& path : data.at("textureSearchPaths")) project.textureSearchPaths.push_back(path.get<std::string>());
    if (!project.startupScene.empty() && AssetPaths::FromUtf8(project.startupScene).is_absolute())
        throw std::runtime_error("startupScene must be relative to the project folder");
    return project;
}

void Project::Save(const ProjectInfo& project)
{
    json data{
        { "formatVersion", project.formatVersion },
        { "name", project.name },
        { "startupScene", project.startupScene },
        { "textureSearchPaths", project.textureSearchPaths },
    };
    // Keep unknown fields that newer tools may have written.
    if (fs::is_regular_file(project.file))
    {
        try
        {
            json previous = ReadJson(project.file);
            if (previous.is_object())
            {
                previous.update(data);
                data = std::move(previous);
            }
        }
        catch (const std::exception&) {}
    }
    WriteJson(project.file, data);
}

void Project::WriteStarterScene(const fs::path& file)
{
    const json scene = json::parse(R"({
  "version": 1,
  "name": "Main",
  "entities": [
    { "tag": "Camera",
      "transform": { "position": [0, 4, -10], "rotation": [0.3, 0, 0], "scale": [1, 1, 1] },
      "camera": { "fov": 1.0, "nearZ": 0.1, "farZ": 1000, "aspectRatio": 1.7777778 } },
    { "tag": "Sun",
      "transform": { "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1] },
      "directionalLight": { "color": [1, 0.96, 0.9], "intensity": 1.2, "direction": [-0.5, -0.8, 0.3], "enabled": true } },
    { "tag": "Ground",
      "transform": { "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [20, 1, 20] },
      "mesh": { "source": "primitive", "primitive": "plane",
        "material": { "name": "GroundMaterial", "color": [0.55, 0.57, 0.6], "roughness": 0.8 } } }
  ]
})");
    auto copy = scene;
    copy["name"] = AssetPaths::ToUtf8(file.stem());
    fs::create_directories(file.parent_path());
    WriteJson(file, copy);
}

ProjectInfo Project::Create(const fs::path& directory, const std::string& name)
{
    if (!IsValidName(name)) throw std::runtime_error("Invalid project name: \"" + name + "\"");
    const auto root = fs::weakly_canonical(fs::absolute(directory));
    if (fs::exists(root) && !fs::is_directory(root))
        throw std::runtime_error(AssetPaths::ToUtf8(root) + " is a file, not a folder");
    fs::create_directories(root);
    if (!FindFile(root).empty()) throw std::runtime_error("Folder already contains a project: " + AssetPaths::ToUtf8(root));
    if (AssetPaths::IsInside(AssetPaths::EngineRoot(), root) || root == AssetPaths::EngineRoot())
        throw std::runtime_error("Create projects outside the engine folder");
    for (const char* folder : Folders) fs::create_directories(root / folder);

    ProjectInfo project;
    project.root = root;
    project.file = root / (AssetPaths::FromUtf8(name).native() + Extension);
    project.name = name;
    project.formatVersion = FormatVersion;
    project.startupScene = DefaultScene;
    project.textureSearchPaths = { "Assets/Textures" };
    const auto scene = root / AssetPaths::FromUtf8(project.startupScene);
    if (!fs::exists(scene)) WriteStarterScene(scene);
    {
        std::ofstream ignore(root / ".gitignore", std::ios::app);
        ignore << "# LSEngine: Play sessions and other generated files\nTemp/\n";
    }
    Save(project);
    return Open(project.file);
}

ProjectInfo Project::OpenSample()
{
    return Open(AssetPaths::EngineRoot());
}

void Project::SetActive(const ProjectInfo& project)
{
    g_Active = project;
    AssetPaths::SetProjectRoot(project.root);
}

const ProjectInfo* Project::Active()
{
    return g_Active ? &*g_Active : nullptr;
}
