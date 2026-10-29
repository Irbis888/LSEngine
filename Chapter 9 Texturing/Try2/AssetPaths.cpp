#include "AssetPaths.h"
#include <Windows.h>
#include <stdexcept>

namespace fs = std::filesystem;
fs::path AssetPaths::FromUtf8(const std::string& value)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
std::string AssetPaths::ToUtf8(const fs::path& value)
{
    const auto text = value.generic_u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}
fs::path AssetPaths::FindRoot()
{
    const auto valid = [](const fs::path& path)
    { return fs::is_directory(path / "Scenes") && fs::is_regular_file(path / "Shaders/Default.hlsl"); };
    std::wstring executable(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
    if (!count || count >= executable.size()) throw std::runtime_error("Cannot find executable path");
    executable.resize(count);
    for (fs::path directory = fs::path(executable).parent_path(); !directory.empty();)
    {
        if (valid(directory)) return fs::weakly_canonical(directory);
        const auto project = directory / "Chapter 9 Texturing/Try2";
        if (valid(project)) return fs::weakly_canonical(project);
        const auto parent = directory.parent_path();
        if (parent == directory) break;
        directory = parent;
    }
    if (valid(fs::current_path())) return fs::weakly_canonical(fs::current_path());
    throw std::runtime_error("Cannot find asset root (Scenes and Shaders/Default.hlsl)");
}
fs::path AssetPaths::Resolve(const fs::path& root, const std::string& value)
{
    if (value.empty() || value.find('\0') != std::string::npos) throw std::runtime_error("Empty/invalid asset path");
    const auto path = FromUtf8(value);
    return fs::weakly_canonical(path.is_absolute() ? path : root / path);
}
static std::string ContainedPath(const fs::path& root, const std::string& value, const wchar_t* folder,
    const wchar_t* extension, const char* kind)
{
    const auto directory = fs::weakly_canonical(root / folder);
    const auto path = AssetPaths::Resolve(root, value);
    auto folderPart = directory.begin(), pathPart = path.begin();
    const auto name = AssetPaths::ToUtf8(folder);
    for (; folderPart != directory.end(); ++folderPart, ++pathPart)
        if (pathPart == path.end() || _wcsicmp(folderPart->c_str(), pathPart->c_str()) != 0)
            throw std::runtime_error(std::string(kind) + " files must be inside the asset root's " + name + " directory");
    if (pathPart == path.end() || _wcsicmp(path.extension().c_str(), extension) != 0)
        throw std::runtime_error("Expected a " + AssetPaths::ToUtf8(extension) + " file inside " + name + "/");
    fs::path relative = folder;
    // Windows path comparison is case-insensitive; lexically_relative is not.
    // Construct the verified suffix instead of emitting ../ for a case change.
    for (; pathPart != path.end(); ++pathPart) relative /= *pathPart;
    return AssetPaths::ToUtf8(relative);
}
std::string AssetPaths::ScriptPath(const fs::path& root, const std::string& value)
{
    return ContainedPath(root, value, L"Scripts", L".lua", "Lua");
}
std::string AssetPaths::PrefabPath(const fs::path& root, const std::string& value)
{
    return ContainedPath(root, value, L"Prefabs", L".json", "Prefab");
}
