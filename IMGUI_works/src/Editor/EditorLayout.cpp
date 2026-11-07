#include "EditorLayout.h"
#include "EditorContext.h"
#include <Engine.h>
#include <AssetPaths.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <Windows.h>
#include <ShlObj.h>
#pragma comment(lib, "Shell32.lib")

namespace
{
    std::filesystem::path settingsDirectory;
    std::string settingsDirectoryUtf8;
    std::string iniPath;
    std::string previousPanels;
    bool resetRequested = false;

    nlohmann::json Panels(const EditorContext& context)
    {
        return {{"hierarchy", context.showHierarchy}, {"inspector", context.showInspector},
            {"viewport", context.showViewport}, {"assets", context.showAssets},
            {"console", context.showConsole}, {"statistics", context.showStatistics},
            {"legend", context.showLegend}};
    }
}

void EditorLayout::Configure(EditorContext& context)
{
    if (!context.engine)
    {
        return;
    }
    const auto projectRoot = std::filesystem::weakly_canonical(context.engine->GetAssetRoot());
    uint64_t projectHash = 14695981039346656037ull;
    for (const unsigned char character : AssetPaths::ToUtf8(projectRoot))
    {
        projectHash = (projectHash ^ character) * 1099511628211ull;
    }
    PWSTR localAppData = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
    if (FAILED(result))
    {
        throw std::runtime_error("Cannot locate editor settings directory");
    }
    std::ostringstream projectIdentifier;
    projectIdentifier << std::hex << projectHash;
    settingsDirectory = std::filesystem::path(localAppData) / "LSEngine/Editor" / projectIdentifier.str();
    CoTaskMemFree(localAppData);
    std::filesystem::create_directories(settingsDirectory);
    settingsDirectoryUtf8 = AssetPaths::ToUtf8(settingsDirectory);
    // Versioned filename intentionally ignores legacy undocked imgui.ini.
    iniPath = AssetPaths::ToUtf8(settingsDirectory / "layout-v1.ini");
    ImGui::GetIO().IniFilename = iniPath.c_str();
    if (std::filesystem::is_regular_file(settingsDirectory / "layout-v1.ini"))
    {
        ImGui::LoadIniSettingsFromDisk(iniPath.c_str());
    }
    std::ifstream input(settingsDirectory / "panels-v1.json");
    auto panels = nlohmann::json::parse(input, nullptr, false);
    if (panels.is_object())
    {
        context.showHierarchy = panels.value("hierarchy", true);
        context.showInspector = panels.value("inspector", true);
        context.showViewport = panels.value("viewport", true);
        context.showAssets = panels.value("assets", true);
        context.showConsole = panels.value("console", true);
        context.showStatistics = panels.value("statistics", false);
        context.showLegend = panels.value("legend", false);
    }
    previousPanels.clear();
}

void EditorLayout::Reset(EditorContext& context)
{
    context.showHierarchy = true;
    context.showInspector = true;
    context.showViewport = true;
    context.showAssets = true;
    context.showConsole = true;
    context.showStatistics = false;
    context.showLegend = false;
    resetRequested = true;
}

void EditorLayout::DrawDockspace(EditorContext& context, float toolbarBottom)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 position(viewport->Pos.x, toolbarBottom);
    ImVec2 size(viewport->Size.x, viewport->Pos.y + viewport->Size.y - toolbarBottom);
    size.y = std::max(size.y, 1.0f);
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Editor Dockspace###EditorDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);
    const ImGuiID dockspace = ImGui::GetID("EditorDockspaceV1");
    if (resetRequested || !ImGui::DockBuilderGetNode(dockspace))
    {
        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace, size);
        ImGuiID center = dockspace;
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, nullptr, &center);
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.3125f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Scene Hierarchy###SceneHierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector###Inspector", right);
        ImGui::DockBuilderDockWindow("Viewport###Viewport", center);
        ImGui::DockBuilderDockWindow("Asset Browser###AssetBrowser", bottom);
        ImGui::DockBuilderDockWindow("Console###Console", bottom);
        ImGui::DockBuilderFinish(dockspace);
        resetRequested = false;
    }
    ImGui::DockSpace(dockspace, ImVec2(0, 0));
    ImGui::End();
    Save(context);
}

void EditorLayout::Save(EditorContext& context)
{
    if (settingsDirectory.empty())
    {
        return;
    }
    const std::string panels = Panels(context).dump(2);
    if (panels != previousPanels)
    {
        std::ofstream(settingsDirectory / "panels-v1.json") << panels;
        previousPanels = panels;
    }
}

const char* EditorLayout::SettingsDirectory()
{
    return settingsDirectoryUtf8.c_str();
}
