#pragma once
#include <filesystem>
#include <string>

namespace AssetPaths
{
    std::filesystem::path FromUtf8(const std::string& value);
    std::string ToUtf8(const std::filesystem::path& value);
    std::filesystem::path FindRoot();
    std::filesystem::path Resolve(const std::filesystem::path& root, const std::string& value);
    // Returns a canonical relative script path; rejects paths outside Scripts/.
    std::string ScriptPath(const std::filesystem::path& root, const std::string& value);
}
