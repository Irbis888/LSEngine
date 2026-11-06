#include "EditorProject.h"
#include "EditorContext.h"
#include "EditorSceneIO.h"
#include "EditorPlayWindow.h"

#include <Engine.h>
#include <AssetPaths.h>
#include <Project.h>
#include <SceneSerializer.h>

#include <Windows.h>
#include <ShObjIdl.h>
#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

#pragma comment(lib, "Ole32.lib")

namespace fs = std::filesystem;

namespace
{
    struct ComScope
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    };

    template<class T> struct ComPtr
    {
        T* p = nullptr;
        ~ComPtr() { if (p) p->Release(); }
        T** operator&() { return &p; }
        T* operator->() const { return p; }
    };

    void SetInitialFolder(IFileDialog* dialog, const fs::path& folder)
    {
        if (folder.empty() || !fs::is_directory(folder)) return;
        ComPtr<IShellItem> item;
        if (SUCCEEDED(SHCreateItemFromParsingName(fs::absolute(folder).c_str(), nullptr, IID_PPV_ARGS(&item))))
            dialog->SetFolder(item.p);
    }

    fs::path Result(IFileDialog* dialog)
    {
        ComPtr<IShellItem> item;
        if (FAILED(dialog->GetResult(&item))) return {};
        PWSTR path = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
        fs::path result(path);
        CoTaskMemFree(path);
        return result;
    }

    HWND Owner() { return GetActiveWindow(); }

    struct NewProjectState
    {
        bool open = false;
        char name[101] = "MyGame";
        char location[4096] = {};
        std::string error;
    } g_NewProject;

    struct OpenProjectState
    {
        bool open = false;
        ProjectInfo project;
    } g_OpenProject;

    std::string Utf8(const fs::path& path) { return AssetPaths::ToUtf8(path); }

    fs::path DefaultProjectsFolder()
    {
        wchar_t profile[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH))
        {
            const auto documents = fs::path(profile) / L"Documents";
            return fs::is_directory(documents) ? documents / L"LSEngine Projects" : fs::path(profile) / L"LSEngine Projects";
        }
        return fs::current_path();
    }

    bool CanChangeScene(EditorContext& ctx)
    {
        if (!ctx.engine) { ctx.statusMessage = "Engine not bound."; return false; }
        if (ctx.mode != EditorMode::Edit) { ctx.statusMessage = "Stop the in-editor simulation first."; return false; }
        if (ctx.engine->IsSceneLoading()) { ctx.statusMessage = "Wait until the scene has loaded."; return false; }
        return true;
    }

    // Project-relative form of a file chosen in a dialog; empty (with a status) when outside.
    std::string InsideProject(EditorContext& ctx, const fs::path& file, const char* what)
    {
        const auto& root = ctx.engine->GetAssetRoot();
        if (!AssetPaths::IsInside(root, file))
        {
            ctx.statusMessage = std::string(what) + " must be inside the project folder " + Utf8(root);
            return {};
        }
        return AssetPaths::MakeProjectRelative(root, Utf8(fs::absolute(file)));
    }
}

fs::path EditorProject::PickFolder(const wchar_t* title, const fs::path& initial)
{
    ComScope com;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title);
    SetInitialFolder(dialog.p, initial);
    if (FAILED(dialog->Show(Owner()))) return {};
    return Result(dialog.p);
}

fs::path EditorProject::PickOpenFile(const wchar_t* title, const fs::path& initial,
    const wchar_t* filterName, const wchar_t* filterPattern)
{
    ComScope com;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    const COMDLG_FILTERSPEC filters[] = { { filterName, filterPattern }, { L"All files", L"*.*" } };
    dialog->SetFileTypes(2, filters);
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR);
    dialog->SetTitle(title);
    SetInitialFolder(dialog.p, initial);
    if (FAILED(dialog->Show(Owner()))) return {};
    return Result(dialog.p);
}

fs::path EditorProject::PickSaveFile(const wchar_t* title, const fs::path& initial, const std::wstring& defaultName,
    const wchar_t* filterName, const wchar_t* filterPattern, const wchar_t* extension)
{
    ComScope com;
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    const COMDLG_FILTERSPEC filters[] = { { filterName, filterPattern } };
    dialog->SetFileTypes(1, filters);
    dialog->SetDefaultExtension(extension);
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_NOCHANGEDIR);
    dialog->SetTitle(title);
    if (!defaultName.empty()) dialog->SetFileName(defaultName.c_str());
    SetInitialFolder(dialog.p, initial);
    if (FAILED(dialog->Show(Owner()))) return {};
    return Result(dialog.p);
}

bool EditorProject::RelaunchWithProject(EditorContext& ctx, const fs::path& projectFile)
{
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
    if (!length || length >= executable.size()) { ctx.statusMessage = "Cannot find the editor executable."; return false; }
    executable.resize(length);
    // --show-editor: the restarted editor opens with its UI visible, like the one it replaces.
    std::wstring command = L"\"" + executable + L"\" --project \"" + projectFile.wstring() + L"\" --show-editor";
    STARTUPINFOW startup{ sizeof(startup) };
    PROCESS_INFORMATION process{};
    const auto workingDirectory = AssetPaths::EngineRoot().wstring();
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
        workingDirectory.c_str(), &startup, &process))
    {
        ctx.statusMessage = "Cannot restart the editor (error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    EditorPlayWindow::Shutdown();
    PostQuitMessage(0);
    return true;
}

void EditorProject::OpenNewProjectDialog(EditorContext& ctx)
{
    (void)ctx;
    g_NewProject.open = true;
    g_NewProject.error.clear();
    if (!g_NewProject.location[0])
        strncpy_s(g_NewProject.location, Utf8(DefaultProjectsFolder()).c_str(), sizeof(g_NewProject.location) - 1);
}

void EditorProject::OpenProjectWithDialog(EditorContext& ctx)
{
    const auto folder = PickFolder(L"Open LSEngine project (folder with a .lsproject file)",
        ctx.engine ? ctx.engine->GetAssetRoot().parent_path() : fs::path());
    if (folder.empty()) return;
    try
    {
        g_OpenProject.project = Project::Open(folder);
        g_OpenProject.open = true;
    }
    catch (const std::exception& error) { ctx.statusMessage = std::string("Open project failed: ") + error.what(); }
}

void EditorProject::DrawProjectDialogs(EditorContext& ctx)
{
    if (g_NewProject.open) { ImGui::OpenPopup("New Project"); g_NewProject.open = false; }
    if (g_OpenProject.open) { ImGui::OpenPopup("Open Project"); g_OpenProject.open = false; }

    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("A project is a folder with <Name>.lsproject and Scenes/, Assets/, Scripts/, Prefabs/.");
        ImGui::InputText("Name", g_NewProject.name, sizeof(g_NewProject.name));
        ImGui::InputText("Location", g_NewProject.location, sizeof(g_NewProject.location));
        ImGui::SameLine();
        if (ImGui::Button("Browse..."))
        {
            const auto folder = PickFolder(L"Choose where to create the project folder",
                AssetPaths::FromUtf8(g_NewProject.location));
            if (!folder.empty()) strncpy_s(g_NewProject.location, Utf8(folder).c_str(), sizeof(g_NewProject.location) - 1);
        }
        const auto target = AssetPaths::FromUtf8(g_NewProject.location) / AssetPaths::FromUtf8(g_NewProject.name);
        ImGui::TextDisabled("Folder: %s", Utf8(target).c_str());
        ImGui::TextDisabled("The editor restarts with the new project; unsaved scene changes are lost.");
        if (!g_NewProject.error.empty()) ImGui::TextColored(ImVec4(1, .45f, .4f, 1), "%s", g_NewProject.error.c_str());
        if (ImGui::Button("Create Project", ImVec2(140, 0)))
        {
            try
            {
                if (!Project::IsValidName(g_NewProject.name)) throw std::runtime_error("Use a name that is a valid folder name.");
                const auto project = Project::Create(target, g_NewProject.name);
                ImGui::CloseCurrentPopup();
                RelaunchWithProject(ctx, project.file);
            }
            catch (const std::exception& error) { g_NewProject.error = error.what(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const auto& project = g_OpenProject.project;
        ImGui::Text("Project: %s", project.name.c_str());
        ImGui::TextDisabled("%s", Utf8(project.file).c_str());
        ImGui::TextDisabled("Startup scene: %s", project.startupScene.empty() ? "(none)" : project.startupScene.c_str());
        ImGui::TextWrapped("The editor restarts with this project; unsaved scene changes are lost.");
        if (ImGui::Button("Open Project", ImVec2(140, 0)))
        {
            ImGui::CloseCurrentPopup();
            RelaunchWithProject(ctx, project.file);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void EditorProject::NewScene(EditorContext& ctx)
{
    if (!CanChangeScene(ctx)) return;
    const auto& root = ctx.engine->GetAssetRoot();
    const auto file = PickSaveFile(L"New scene", root / L"Scenes", L"NewScene.json", L"Scene (*.json)", L"*.json", L"json");
    if (file.empty()) return;
    const auto relative = InsideProject(ctx, file, "Scenes");
    if (relative.empty()) return;
    try { Project::WriteStarterScene(file); }
    catch (const std::exception& error) { ctx.statusMessage = std::string("New scene failed: ") + error.what(); return; }
    EditorSceneIO::LoadScene(ctx, relative.c_str());
}

void EditorProject::OpenSceneWithDialog(EditorContext& ctx)
{
    if (!CanChangeScene(ctx)) return;
    const auto& root = ctx.engine->GetAssetRoot();
    const auto file = PickOpenFile(L"Open scene", root / L"Scenes", L"Scene (*.json)", L"*.json");
    if (file.empty()) return;
    const auto relative = InsideProject(ctx, file, "Scenes");
    if (!relative.empty()) EditorSceneIO::LoadScene(ctx, relative.c_str());
}

void EditorProject::SaveSceneAsWithDialog(EditorContext& ctx)
{
    if (!ctx.engine) return;
    const auto& root = ctx.engine->GetAssetRoot();
    const auto current = ctx.engine->GetScenePath();
    const auto name = current.empty() ? std::wstring(L"Scene.json") : AssetPaths::FromUtf8(current).filename().wstring();
    const auto file = PickSaveFile(L"Save scene as", root / L"Scenes", name, L"Scene (*.json)", L"*.json", L"json");
    if (file.empty()) return;
    const auto relative = InsideProject(ctx, file, "Scenes");
    if (relative.empty()) return;
    EditorSceneIO::SaveScene(ctx, relative.c_str());
    // Later "Save Scene" writes to the new file, like Save As in other editors.
    if (ctx.statusMessage.rfind("Saved", 0) == 0) ctx.engine->SetScenePath(relative);
}

void EditorProject::SetCurrentSceneAsStartup(EditorContext& ctx)
{
    if (!ctx.engine || ctx.engine->GetProject().file.empty() || ctx.engine->GetScenePath().empty()) return;
    try
    {
        auto project = Project::Open(ctx.engine->GetProject().file);
        project.startupScene = ctx.engine->GetScenePath();
        Project::Save(project);
        ctx.statusMessage = "Startup scene: " + project.startupScene;
    }
    catch (const std::exception& error) { ctx.statusMessage = std::string("Saving the project failed: ") + error.what(); }
}

std::vector<std::string> EditorProject::ListScenes(EditorContext& ctx)
{
    std::vector<std::string> scenes;
    if (!ctx.engine) return scenes;
    const auto& root = ctx.engine->GetAssetRoot();
    const auto add = [&](const fs::path& folder)
    {
        std::error_code error;
        if (!fs::is_directory(folder, error)) return;
        for (auto it = fs::recursive_directory_iterator(folder, error); !error && it != fs::recursive_directory_iterator(); it.increment(error))
        {
            if (!it->is_regular_file() || _wcsicmp(it->path().extension().c_str(), L".json") != 0) continue;
            const auto name = it->path().filename().wstring();
            if (name == L"EditorPlaySnapshot.json") continue; // Internal in-editor simulation snapshot.
            scenes.push_back(AssetPaths::MakeProjectRelative(root, Utf8(it->path())));
        }
    };
    add(root / L"Scenes");
    std::error_code error;
    if (fs::is_directory(root / L"Games", error))
        for (const auto& game : fs::directory_iterator(root / L"Games", error))
            if (game.is_directory()) add(game.path() / L"Scenes");
    std::sort(scenes.begin(), scenes.end());
    return scenes;
}

std::string EditorProject::ImportTexture(EditorContext& ctx, const fs::path& file)
{
    const auto& root = ctx.engine->GetAssetRoot();
    if (AssetPaths::IsInside(root, file)) return AssetPaths::MakeProjectRelative(root, Utf8(fs::absolute(file)));
    const auto folder = root / L"Assets" / L"Textures";
    fs::create_directories(folder);
    auto target = folder / file.filename();
    const auto sameBytes = [](const fs::path& a, const fs::path& b)
    {
        if (fs::file_size(a) != fs::file_size(b)) return false;
        std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
        return std::equal(std::istreambuf_iterator<char>(x), {}, std::istreambuf_iterator<char>(y));
    };
    for (int suffix = 2; fs::exists(target) && !sameBytes(file, target); ++suffix)
        target = folder / (file.stem().wstring() + L"_" + std::to_wstring(suffix) + file.extension().wstring());
    if (!fs::exists(target)) fs::copy_file(file, target);
    return AssetPaths::MakeProjectRelative(root, Utf8(target));
}

bool EditorProject::ApplyMaterial(EditorContext& ctx, entt::entity entity, const MaterialDesc& material)
{
    if (!ctx.registry || !ctx.resources || !ctx.registry->valid(entity)) return false;
    auto* mesh = ctx.registry->try_get<MeshComponent>(entity);
    auto* source = ctx.registry->try_get<MeshSourceComponent>(entity);
    if (!mesh || !source) { ctx.statusMessage = "Material: the entity has no saved mesh description."; return false; }
    try
    {
        const MaterialID id = ctx.resources->CreateTexturedMaterial(material);
        ctx.resources->SetMeshMaterial(mesh->meshID, id);
        source->mesh.material = material;
        return true;
    }
    catch (const std::exception& error)
    {
        ctx.statusMessage = std::string("Material update failed: ") + error.what();
        return false;
    }
}
