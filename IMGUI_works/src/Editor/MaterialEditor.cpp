#include "MaterialEditor.h"
#include "EditorContext.h"
#include "EditorProject.h"
#include <AssetPaths.h>
#include <Engine.h>
#include <D3DRenderAdapter.h>
#include <SceneSerializer.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr const wchar_t* MaterialFilter = L"LS Material (*.lsmaterial)";
    constexpr const wchar_t* MaterialPattern = L"*.lsmaterial";

    struct MaterialDocument
    {
        MaterialDesc description;
        std::string path;
        std::string message;
        std::optional<std::string> pendingOpen;
        MaterialID previewMaterial = 0;
        MeshID previewMesh = 0;
        float rotation = 0.0f;
        bool dirty = false;
        bool previewChanged = true;
    };

    MaterialDocument document;

    std::string MaterialPath(EditorContext& context, const std::filesystem::path& file)
    {
        const auto& root = context.engine->GetAssetRoot();
        if (_wcsicmp(file.extension().c_str(), L".lsmaterial") != 0 || !AssetPaths::IsInside(root, file))
        {
            throw std::runtime_error("Choose a .lsmaterial file inside the project.");
        }
        return AssetPaths::MakeProjectRelative(root, AssetPaths::ToUtf8(file));
    }

    void ReplaceDocument(EditorContext& context, const std::string& path)
    {
        try
        {
            MaterialDesc next;
            next.name = "New Material";
            if (!path.empty())
            {
                next = SceneSerializer::ReadMaterialAsset(AssetPaths::Resolve(context.engine->GetAssetRoot(), path));
            }
            if (!next.surface)
            {
                next.surface.emplace();
            }
            document.description = std::move(next);
            document.path = path;
            document.dirty = false;
            document.previewChanged = true;
            document.message.clear();
        }
        catch (const std::exception& error)
        {
            document.message = error.what();
        }
    }

    void SaveDocument(EditorContext& context, bool saveAs)
    {
        try
        {
            std::string path = document.path;
            if (path.empty() || saveAs)
            {
                const auto directory = context.engine->GetAssetRoot() / L"Assets" / L"Materials";
                std::filesystem::create_directories(directory);
                const auto file = EditorProject::PickSaveFile(L"Save material", directory, L"Material.lsmaterial",
                    MaterialFilter, MaterialPattern, L"lsmaterial");
                if (file.empty())
                {
                    return;
                }
                path = MaterialPath(context, file);
            }
            context.resources->SaveMaterialAsset(path, document.description);
            document.path = std::move(path);
            document.message = "Saved " + document.path;
            document.dirty = false;
            context.statusMessage = document.message;
        }
        catch (const std::exception& error)
        {
            document.message = error.what();
        }
    }

    void AssignFile(EditorContext& context, entt::entity entity, const std::string& path)
    {
        try
        {
            const MaterialID material = context.resources->LoadMaterialAsset(path);
            context.registry->emplace_or_replace<MaterialComponent>(entity, MaterialComponent{path, material, {}});
            context.statusMessage = "Assigned " + path;
        }
        catch (const std::exception& error)
        {
            context.statusMessage = error.what();
            document.message = error.what();
        }
    }

    std::filesystem::path PickMaterial(EditorContext& context)
    {
        return EditorProject::PickOpenFile(L"Open material", context.engine->GetAssetRoot() / "Assets/Materials",
            MaterialFilter, MaterialPattern);
    }

    bool EditString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0)
    {
        char buffer[4096]{};
        strncpy_s(buffer, value.c_str(), _TRUNCATE);
        if (ImGui::InputText(label, buffer, sizeof(buffer), flags))
        {
            value = buffer;
            return true;
        }
        return false;
    }

    bool DrawMap(EditorContext& context, const char* label, std::wstring& path, TextureID previewTexture)
    {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        char buffer[4096]{};
        strncpy_s(buffer, AssetPaths::ToUtf8(path).c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 9);
        bool changed = false;
        if (ImGui::InputText("##Path", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            path = AssetPaths::FromUtf8(buffer).wstring();
            changed = true;
        }
        ImGui::SetItemTooltip("Enter a path relative to the project and press Enter.");
        ImGui::SameLine();
        if (ImGui::Button("Browse"))
        {
            const auto file = EditorProject::PickOpenFile(L"Choose texture", context.engine->GetAssetRoot() / "Assets/Textures",
                L"Images", L"*.png;*.tga;*.dds;*.jpg;*.jpeg;*.bmp");
            if (!file.empty())
            {
                try
                {
                    path = AssetPaths::FromUtf8(EditorProject::ImportTexture(context, file)).wstring();
                    changed = true;
                }
                catch (const std::exception& error)
                {
                    document.message = error.what();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear") && !path.empty())
        {
            path.clear();
            changed = true;
        }
        if (!changed && previewTexture)
        {
            const auto& texture = context.resources->GetTexture(previewTexture);
            if (texture.state == ResourceState::Failed)
            {
                ImGui::TextWrapped("%s", texture.error.c_str());
            }
            else if (texture.state != ResourceState::Ready)
            {
                ImGui::TextDisabled("Loading texture...");
            }
        }
        ImGui::PopID();
        return changed;
    }

    bool DrawRenderState(MaterialRenderState& state)
    {
        if (!ImGui::CollapsingHeader("Render State", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return false;
        }
        int preset = state == MaterialRenderState{} ? 0 :
            (state == MaterialRenderState::AlphaBlend() ? 1 : (state == MaterialRenderState::Additive() ? 2 : 3));
        const char* presets[] = {"Opaque", "Alpha Blend", "Additive", "Custom"};
        bool changed = false;
        const auto setControlWidth = [](const char* label)
        {
            const float labelWidth = ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::SetNextItemWidth(std::max(70.0f, ImGui::GetContentRegionAvail().x - labelWidth));
        };
        setControlWidth("Preset");
        if (ImGui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets)) && preset != 3)
        {
            state = preset == 0 ? MaterialRenderState{} :
                (preset == 1 ? MaterialRenderState::AlphaBlend() : MaterialRenderState::Additive());
            changed = true;
        }
        ImGui::SetItemTooltip("Opaque: replace + write depth. Alpha Blend: RGB = S * Sa + D * (1 - Sa), A = Sa + Da * (1 - Sa).\nAdditive: RGB = S * Sa + D, A = Sa + Da. Blended presets test depth and do not write it.\nPresets fill the fields below. Manual differences are Custom.");
        changed |= ImGui::Checkbox("Depth Test", &state.depthTest);
        ImGui::SetItemTooltip("Compare fragment depth to existing depth. Disabled + Write enabled means unconditional depth writes.");
        changed |= ImGui::Checkbox("Depth Write", &state.depthWrite);
        ImGui::SetItemTooltip("Write passing fragments to depth. Usually disabled for transparency; independent of Depth Test.");
        const auto combo = [&](const char* label, auto& value, const auto& names, const char* tooltip)
        {
            int index = static_cast<int>(value);
            setControlWidth(label);
            const bool edited = ImGui::Combo(label, &index, names.data(), static_cast<int>(names.size()));
            ImGui::SetItemTooltip("%s", tooltip);
            if (edited)
            {
                value = static_cast<std::remove_reference_t<decltype(value)>>(index);
            }
            return edited;
        };
        changed |= combo("Depth Compare", state.depthCompare, DepthComparisonNames,
            "Compare incoming depth against stored depth. Less is nearer with this engine's normal depth buffer. Ignored when testing is disabled.");
        changed |= ImGui::Checkbox("Blend Enabled", &state.blendEnabled);
        ImGui::SetItemTooltip("Blend color after opaque/masked geometry. Objects in the same render-order layer draw back to front. Entity IDs never blend.");
        ImGui::BeginDisabled(!state.blendEnabled);
        changed |= combo("RGB Source", state.sourceColor, BlendFactorNames, "Multiply source RGB by this factor before the operation.");
        changed |= combo("RGB Destination", state.destinationColor, BlendFactorNames, "Multiply existing RGB by this factor before the operation.");
        changed |= combo("RGB Operation", state.colorOperation, BlendOperationNames,
            "Add: S + D; Subtract: S - D; ReverseSubtract: D - S. Min/Max compare unfactored values (factors ignored).");
        changed |= combo("Alpha Source", state.sourceAlpha, BlendFactorNames,
            "Source alpha factor. Color factors use their alpha component on this scalar channel.");
        changed |= combo("Alpha Destination", state.destinationAlpha, BlendFactorNames,
            "Destination alpha factor. Color factors use their alpha component on this scalar channel.");
        changed |= combo("Alpha Operation", state.alphaOperation, BlendOperationNames,
            "Operation on alpha independent of RGB. Min/Max ignore factors.");
        ImGui::EndDisabled();
        return changed;
    }

    bool DrawProperties(EditorContext& context)
    {
        auto& description = document.description;
        auto& base = *description.surface;
        auto& parameters = base.defaults;
        const MaterialParameters applied = document.previewMaterial
            ? context.resources->GetMaterial(document.previewMaterial).parameters : MaterialParameters{};

        bool changed = EditString("Name", description.name);
        if (ImGui::CollapsingHeader("Shader and rendering", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto path = std::filesystem::path(base.program.file).lexically_normal().generic_wstring();
            int shader = path == L"Shaders/Default.hlsl" ? 0 : (path == L"Shaders/Unlit.hlsl" ? 1 : 2);
            const char* shaderNames[] = {"Lit (GGX)", "Unlit", "Custom HLSL"};
            if (ImGui::Combo("Shader", &shader, shaderNames, IM_ARRAYSIZE(shaderNames)))
            {
                base.program = SurfaceProgram{};
                if (shader == 1)
                {
                    base.program.file = L"Shaders/Unlit.hlsl";
                }
                else if (shader == 2)
                {
                    base.program.file = L"Shaders/Custom.hlsl";
                }
                changed = true;
            }
            if (shader == 2)
            {
                std::string file = AssetPaths::ToUtf8(base.program.file);
                char shaderPath[4096]{};
                strncpy_s(shaderPath, file.c_str(), _TRUNCATE);
                if (ImGui::InputText("HLSL file", shaderPath, sizeof(shaderPath), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    base.program.file = AssetPaths::FromUtf8(shaderPath).wstring();
                    changed = true;
                }
                if (ImGui::Button("Browse HLSL"))
                {
                    const auto selected = EditorProject::PickOpenFile(L"Choose surface shader", context.engine->GetAssetRoot(),
                        L"HLSL (*.hlsl)", L"*.hlsl");
                    if (!selected.empty())
                    {
                        base.program.file = AssetPaths::FromUtf8(
                            AssetPaths::MakeProjectRelative(context.engine->GetAssetRoot(), AssetPaths::ToUtf8(selected))).wstring();
                        changed = true;
                    }
                }
                changed |= EditString("Vertex entry", base.program.vertexEntry, ImGuiInputTextFlags_EnterReturnsTrue);
                changed |= EditString("Pixel entry", base.program.pixelEntry, ImGuiInputTextFlags_EnterReturnsTrue);
                changed |= ImGui::Checkbox("Editor variant", &base.program.editorVariant);
                changed |= ImGui::Checkbox("Masked variant", &base.program.maskedVariant);
                if (ImGui::Button("Retry shader compilation"))
                {
                    context.engine->GetRenderer().ReloadShaders();
                }
                const auto* renderer = dynamic_cast<const D3DRenderAdapter*>(&context.engine->GetRenderer());
                if (renderer && !renderer->GetSurfaceDiagnostic().empty())
                {
                    ImGui::TextWrapped("%s", renderer->GetSurfaceDiagnostic().c_str());
                }
            }

            int mode = base.mode == SurfaceMode::Masked ? 1 : 0;
            const char* modes[] = {"Opaque", "Masked"};
            if (ImGui::Combo("Surface mode", &mode, modes, IM_ARRAYSIZE(modes)))
            {
                base.mode = mode == 1 ? SurfaceMode::Masked : SurfaceMode::Opaque;
                changed = true;
            }
            changed |= ImGui::Checkbox("Two sided", &base.twoSided);
            if (base.mode == SurfaceMode::Masked)
            {
                changed |= ImGui::SliderFloat("Alpha cutoff", &parameters.alphaCutoff, 0, 1);
            }
        }

        changed |= DrawRenderState(base.renderState);

        if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
        {
            glm::vec4 color(description.color, parameters.baseColor.a);
            if (ImGui::ColorEdit4("Base color", &color.x))
            {
                description.color = glm::vec3(color);
                parameters.baseColor = color;
                changed = true;
            }
            changed |= DrawMap(context, "Base color map", description.albedoTexture, applied.albedo);
            changed |= ImGui::SliderFloat("Roughness", &description.roughness, 0, 1);
            bool automaticMetallic = !parameters.metallic;
            if (ImGui::Checkbox("Automatic metallic factor", &automaticMetallic))
            {
                parameters.metallic = automaticMetallic ? std::nullopt :
                    std::optional<float>(description.ormTexture.empty() ? 0.0f : 1.0f);
                changed = true;
            }
            if (parameters.metallic)
            {
                changed |= ImGui::SliderFloat("Metallic", &*parameters.metallic, 0, 1);
            }
            else
            {
                ImGui::TextDisabled("Metallic factor follows ORM availability.");
            }
            changed |= ImGui::SliderFloat("AO strength", &parameters.aoStrength, 0, 1);
            changed |= DrawMap(context, "ORM  (R: AO, G: Roughness, B: Metallic)", description.ormTexture, applied.orm);
            changed |= ImGui::SliderFloat("Normal strength", &parameters.normalStrength, 0, 8);
            changed |= DrawMap(context, "Normal map", description.normalTexture, applied.normal);
        }
        if (ImGui::CollapsingHeader("Emissive and UV", ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= ImGui::ColorEdit3("Emissive color", &parameters.emissiveColor.x);
            changed |= ImGui::DragFloat("Emissive intensity", &parameters.emissiveIntensity, 0.05f, 0, 100, "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Emissive light range", &parameters.emissiveLightRange, 0.1f, 0, 1000,
                "%.1f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip("World units. Set to zero to keep surface emission only.");
            changed |= DrawMap(context, "Emissive map", description.emissiveTexture, applied.emissive);
            changed |= ImGui::DragFloat2("UV scale", &parameters.uvScale.x, 0.02f);
            changed |= ImGui::DragFloat2("UV offset", &parameters.uvOffset.x, 0.01f);
        }
        return changed;
    }
}

void Editor_OpenMaterial(EditorContext& context, const std::string& path)
{
    context.showMaterialEditor = true;
    ImGui::SetWindowFocus("Material Editor###MaterialEditor");
    if (document.dirty)
    {
        document.pendingOpen = path;
    }
    else
    {
        ReplaceDocument(context, path);
    }
}

void Editor_DrawMaterialWindow(EditorContext& context)
{
    context.materialPreviewMesh = 0;
    if (!context.showMaterialEditor || !context.resources || !context.engine)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(850, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(1.0f);
    const auto flags = document.dirty ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None;
    if (!ImGui::Begin("Material Editor###MaterialEditor", &context.showMaterialEditor, flags))
    {
        ImGui::End();
        return;
    }
    if (!document.description.surface)
    {
        ReplaceDocument(context, {});
    }

    if (ImGui::Button("New"))
    {
        Editor_OpenMaterial(context, {});
    }
    ImGui::SameLine();
    if (ImGui::Button("Open..."))
    {
        const auto file = PickMaterial(context);
        if (!file.empty())
        {
            try
            {
                Editor_OpenMaterial(context, MaterialPath(context, file));
            }
            catch (const std::exception& error)
            {
                document.message = error.what();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        SaveDocument(context, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As..."))
    {
        SaveDocument(context, true);
    }
    ImGui::SameLine();
    const bool canAssign = Editor_CanEditComponents() && !document.path.empty() && !document.dirty &&
        context.registry && context.registry->valid(context.selected) && context.registry->all_of<MeshComponent>(context.selected);
    ImGui::BeginDisabled(!canAssign);
    if (ImGui::Button("Apply to selected"))
    {
        AssignFile(context, context.selected, document.path);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", document.path.empty() ? "Unsaved material" : document.path.c_str());
    if (!document.message.empty())
    {
        ImGui::TextWrapped("%s", document.message.c_str());
    }
    if (document.pendingOpen)
    {
        ImGui::OpenPopup("Discard material changes?");
    }
    if (ImGui::BeginPopupModal("Discard material changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("The current material has unsaved edits.");
        if (ImGui::Button("Discard"))
        {
            ReplaceDocument(context, *document.pendingOpen);
            document.pendingOpen.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            document.pendingOpen.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    const float previewWidth = std::min(360.0f, ImGui::GetContentRegionAvail().x * 0.43f);
    if (ImGui::BeginChild("Sphere preview", ImVec2(previewWidth, 0), ImGuiChildFlags_Borders))
    {
        ImGui::TextUnformatted("Sphere preview");
        const uint64_t texture = context.engine->GetRenderer().MaterialPreviewTexture();
        if (texture)
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float imageSize = std::max(1.0f, std::min(available.x, available.y - ImGui::GetFrameHeightWithSpacing() * 2));
            ImGui::Image(ImTextureID(texture), ImVec2(imageSize, imageSize));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                document.rotation = std::remainder(document.rotation + ImGui::GetIO().MouseDelta.x * 0.015f, 6.2831853f);
            }
            ImGui::TextDisabled("Drag to rotate");
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderAngle("##PreviewRotation", &document.rotation, -180, 180, "%.0f deg");
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("Material properties"))
    {
        if (DrawProperties(context))
        {
            document.dirty = true;
            document.previewChanged = true;
        }
    }
    ImGui::EndChild();

    if (document.previewChanged)
    {
        try
        {
            if (!document.previewMaterial)
            {
                document.previewMaterial = context.resources->CreateTexturedMaterial(document.description);
                document.previewMesh = context.resources->CreateSphere(document.previewMaterial, 64, 32);
            }
            else
            {
                context.resources->UpdateTexturedMaterial(document.previewMaterial, document.description);
            }
        }
        catch (const std::exception& error)
        {
            document.message = error.what();
        }
        document.previewChanged = false;
    }
    context.materialPreviewMesh = document.previewMesh;
    context.materialPreviewRotation = document.rotation;
    ImGui::End();
}

void Editor_DrawMaterialComponent(EditorContext& context, entt::entity entity)
{
    const auto* material = context.registry->try_get<MaterialComponent>(entity);
    if (material)
    {
        if (!ImGui::CollapsingHeader("Material Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }
        ImGui::TextWrapped("%s", material->assetPath.c_str());
        if (!material->error.empty())
        {
            ImGui::TextWrapped("%s", material->error.c_str());
        }
        if (ImGui::Button("Edit material"))
        {
            Editor_OpenMaterial(context, material->assetPath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove material component"))
        {
            context.registry->remove<MaterialComponent>(entity);
            return;
        }
    }
    if (ImGui::Button(material ? "Replace material file..." : "Add material component..."))
    {
        const auto file = PickMaterial(context);
        if (!file.empty())
        {
            try
            {
                AssignFile(context, entity, MaterialPath(context, file));
            }
            catch (const std::exception& error)
            {
                context.statusMessage = error.what();
            }
        }
    }
}
