#pragma once
#include <filesystem>
#include <string>

namespace AssetPaths
{
    std::filesystem::path FromUtf8(const std::string& value);
    std::string ToUtf8(const std::filesystem::path& value);
    // Engine installation: the directory with Shaders/Default.hlsl (and the sample project).
    std::filesystem::path EngineRoot();
    // Asset root of the active project (see Project.h); EngineRoot() when no project is active.
    std::filesystem::path FindRoot();
    void SetProjectRoot(const std::filesystem::path& root);
    const std::filesystem::path& ProjectRoot(); // Empty when no project is active.
    // True when path (absolute or relative to root) lies inside root; case-insensitive on Windows.
    bool IsInside(const std::filesystem::path& root, const std::filesystem::path& path);
    // Form written into scene/project files: a generic relative path when the file lies
    // inside root, otherwise the input unchanged (relative paths are kept as written).
    std::string MakeProjectRelative(const std::filesystem::path& root, const std::string& value);
    std::filesystem::path Resolve(const std::filesystem::path& root, const std::string& value);
    // Validates a relative script path in Scripts/ or Games/<name>/Scripts/ without file I/O.
    std::string NormalizeScriptPath(const std::string& value);
    // Returns a canonical relative path in one of the allowed script directories.
    std::string ScriptPath(const std::filesystem::path& root, const std::string& value);
    // Entity templates live in Prefabs/ or Games/<name>/Prefabs/.
    std::string PrefabPath(const std::filesystem::path& root, const std::string& value);
}
