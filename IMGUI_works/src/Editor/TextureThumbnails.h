#pragma once
#include "EditorContext.h"
#include <Engine.h>
#include <ResourceManager.h>
#include <imgui.h>
#include <algorithm>
#include <cwctype>

inline bool Editor_IsTextureAsset(const std::filesystem::path& file)
{
    std::wstring extension = file.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    for (const wchar_t* supported : {L".dds", L".png", L".tga", L".jpg", L".jpeg", L".jpe", L".jfif",
        L".bmp", L".dib", L".psd", L".gif", L".hdr", L".pic", L".pnm", L".ppm", L".pgm"})
    {
        if (extension == supported)
        {
            return true;
        }
    }
    return false;
}

inline void Editor_DrawTextureThumbnail(EditorContext& context, const std::wstring& path, TextureID textureID = 0, float size = 64)
{
    if (!context.resources || !context.engine || (path.empty() && !textureID))
    {
        return;
    }
    try
    {
        if (!textureID && !path.empty())
        {
            textureID = context.resources->LoadTexture(path);
        }
        const auto& texture = context.resources->GetTexture(textureID);
        const auto& image = texture.imageData;
        const uint64_t descriptor = context.engine->GetRenderer().TexturePreview(textureID);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        auto* drawList = ImGui::GetWindowDrawList();
        for (int row = 0; row < 8; ++row)
        {
            for (int column = 0; column < 8; ++column)
            {
                const float cell = size / 8;
                drawList->AddRectFilled(ImVec2(origin.x + column * cell, origin.y + row * cell),
                    ImVec2(origin.x + (column + 1) * cell, origin.y + (row + 1) * cell),
                    (row + column) % 2 == 0 ? IM_COL32(75, 75, 75, 255) : IM_COL32(110, 110, 110, 255));
            }
        }
        ImGui::Dummy(ImVec2(size, size));
        if (descriptor)
        {
            const float scale = size / std::max(1u, std::max(image.width, image.height));
            const ImVec2 extent(image.width * scale, image.height * scale);
            const ImVec2 minimum(origin.x + (size - extent.x) * 0.5f, origin.y + (size - extent.y) * 0.5f);
            drawList->AddImage(ImTextureID(descriptor), minimum, ImVec2(minimum.x + extent.x, minimum.y + extent.y));
        }
        else
        {
            drawList->AddText(ImVec2(origin.x + 3, origin.y + size * 0.4f), IM_COL32_WHITE,
                texture.state == ResourceState::Failed ? "Failed" : (texture.state == ResourceState::Ready ? "Preview..." : "Loading"));
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("%u x %u", image.width, image.height);
            if (descriptor)
            {
                const float scale = 256.0f / std::max(1u, std::max(image.width, image.height));
                ImGui::Image(ImTextureID(descriptor), ImVec2(image.width * scale, std::max(1.0f, image.height * scale)));
            }
            if (image.isCubeMap || image.arraySize > 1 || image.dimension != ImageDimension::Texture2D)
            {
                ImGui::TextUnformatted("Preview: first face / array element / volume slice.");
            }
            if (!texture.error.empty())
            {
                ImGui::TextWrapped("%s", texture.error.c_str());
            }
            ImGui::EndTooltip();
        }
    }
    catch (const std::exception& error)
    {
        ImGui::TextWrapped("%s", error.what());
    }
}
