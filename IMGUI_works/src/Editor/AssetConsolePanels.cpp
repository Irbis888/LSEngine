#include "EditorContext.h"
#include "EditorSceneIO.h"
#include "EditorProject.h"
#include <Engine.h>
#include <AssetPaths.h>
#include <ScriptSystem.h>
#include <ScriptComponent.h>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <imgui.h>

void Editor_DrawAssetsAndConsole(EditorContext& context)
{
    if (context.showAssets)
    {
        if (ImGui::Begin("Asset Browser###AssetBrowser", &context.showAssets) && context.engine)
        {
            static std::filesystem::path relativeDirectory;
            const auto root = context.engine->GetAssetRoot();
            ImGui::TextDisabled("%s", AssetPaths::ToUtf8(root / relativeDirectory).c_str());
            if (!relativeDirectory.empty() && ImGui::Button("Up"))
            {
                relativeDirectory = relativeDirectory.parent_path();
            }
            std::error_code error;
            std::vector<std::filesystem::directory_entry> entries;
            for (const auto& entry : std::filesystem::directory_iterator(root / relativeDirectory, error))
            {
                if (!entry.path().filename().wstring().starts_with(L".") && !entry.is_symlink())
                {
                    entries.push_back(entry);
                }
            }
            std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right)
            {
                if (left.is_directory() != right.is_directory())
                {
                    return left.is_directory();
                }
                return left.path().filename() < right.path().filename();
            });
            if (entries.empty())
            {
                ImGui::TextDisabled("This folder is empty.");
            }
            for (const auto& entry : entries)
            {
                const auto relativePath = entry.path().lexically_relative(root);
                const std::string label = (entry.is_directory() ? "[Folder] " : "") + AssetPaths::ToUtf8(entry.path().filename());
                ImGui::Selectable(label.c_str());
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (entry.is_directory())
                    {
                        relativeDirectory = relativePath;
                        break;
                    }
                    const auto path = AssetPaths::ToUtf8(relativePath);
                    if (entry.path().extension() == L".json" && path.find("Scenes/") != std::string::npos && Editor_CanEditComponents())
                    {
                        EditorSceneIO::LoadScene(context, path.c_str());
                    }
                }
            }
        }
        ImGui::End();
    }
    if (context.showConsole)
    {
        if (ImGui::Begin("Console###Console", &context.showConsole))
        {
            static std::vector<std::string> messages;
            static std::string previousMessage;
            if (ImGui::Button("Clear"))
            {
                messages.clear();
            }
            if (!context.statusMessage.empty() && context.statusMessage != previousMessage)
            {
                messages.push_back(context.statusMessage);
                previousMessage = context.statusMessage;
                if (messages.size() > 256)
                {
                    messages.erase(messages.begin());
                }
            }
            for (const auto& message : messages)
            {
                ImGui::TextWrapped("%s", message.c_str());
            }
            if (context.engine && context.registry)
            {
                const auto& error = context.engine->GetSceneLoadProgress().error;
                if (!error.empty())
                {
                    ImGui::TextWrapped("Scene: %s", error.c_str());
                }
                for (auto entity : context.registry->view<ScriptComponent>())
                {
                    const auto scriptError = context.engine->GetScripts().GetError(entity);
                    if (!scriptError.empty())
                    {
                        ImGui::TextWrapped("Lua: %s", scriptError.c_str());
                    }
                }
            }
            if (messages.empty())
            {
                ImGui::TextDisabled("No editor messages.");
            }
        }
        ImGui::End();
    }
}
