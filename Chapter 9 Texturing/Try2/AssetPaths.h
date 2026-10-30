#pragma once
#include <filesystem>
#include <string>

namespace AssetPaths
{
    std::filesystem::path FromUtf8(const std::string& value);
    std::string ToUtf8(const std::filesystem::path& value);
    std::filesystem::path FindRoot();
    std::filesystem::path Resolve(const std::filesystem::path& root, const std::string& value);
    // Validates a relative script path in Scripts/ or Games/<name>/Scripts/ without file I/O.
    std::string NormalizeScriptPath(const std::string& value);
    // Returns a canonical relative path in one of the allowed script directories.
    std::string ScriptPath(const std::filesystem::path& root, const std::string& value);
    // Entity templates live in Prefabs/ or Games/<name>/Prefabs/.
    std::string PrefabPath(const std::filesystem::path& root, const std::string& value);
}
