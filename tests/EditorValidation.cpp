#include "EditorValidation.h"
#include <Engine.h>
#include <AssetPaths.h>
#include <TerrainSystem.h>
#include <ResourceManager.h>
#include <Editor/EditorContext.h>
#include <Editor/EditorLayout.h>
#include <Editor/MaterialEditor.h>
#include <SceneSerializer.h>
#include <Editor/EditorPlayWindow.h>
#include <imgui_internal.h>
#include <d3d12sdklayers.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool valid, const char* message)
    {
        if (!valid)
        {
            throw std::runtime_error(message);
        }
    }

    entt::entity Find(entt::registry& registry, const char* name)
    {
        for (auto entity : registry.view<TagComponent>())
        {
            if (registry.get<TagComponent>(entity).tag == name)
            {
                return entity;
            }
        }
        throw std::runtime_error(std::string("Missing test entity: ") + name);
    }

    void Pick(EditorContext& context, float normalizedX, float normalizedY)
    {
        auto& viewport = context.viewport;
        const uint64_t request = ++viewport.selectionRevision;
        viewport.pendingPick = request;
        context.engine->GetRenderer().RequestViewportPick(
            uint32_t(normalizedX * viewport.width), uint32_t(normalizedY * viewport.height),
            request, context.engine->GetSceneGeneration());
    }

    void PickWorld(EditorContext& context, const glm::vec3& worldPosition)
    {
        const float tangent = std::tan(1.05f * 0.5f);
        const float distance = worldPosition.z + 12;
        const float aspect = float(context.viewport.width) / context.viewport.height;
        Pick(context, 0.5f + worldPosition.x / (2 * distance * tangent * aspect),
            0.5f - worldPosition.y / (2 * distance * tangent));
    }
    void CheckDebugLayer(D3DRenderAdapter& renderer)
    {
        ComPtr<ID3D12InfoQueue> queue;
        if (FAILED(renderer.GetImGuiBindings().Device->QueryInterface(IID_PPV_ARGS(&queue))))
        {
            return;
        }
        for (UINT64 index = 0; index < queue->GetNumStoredMessages(); ++index)
        {
            SIZE_T bytes = 0;
            queue->GetMessage(index, nullptr, &bytes);
            std::vector<char> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            queue->GetMessage(index, message, &bytes);
            Check(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION, message->pDescription);
        }
    }

}

void EditorValidation::Tick(Engine& engine, D3DRenderAdapter& renderer)
{
    Check(++totalFrames < 2500, "Editor validation timed out");
    auto& context = *Editor_GetContext();
    auto& registry = engine.GetRegistry();
    CaptureCompleted(renderer);
    if (engine.IsSceneLoading())
    {
        return;
    }
    Check(engine.GetSceneLoadProgress().error.empty(), "Scene loading failed");
    if (renderStateTest)
    {
        CheckRenderStates(engine, renderer);
        return;
    }
    if (materialTest)
    {
        CheckMaterials(engine, renderer);
        return;
    }
    if (preview)
    {
        if (stage == 0 && ++frames >= 150)
        {
            output = std::filesystem::temp_directory_path() / ("LSEditorPreview-" + std::to_string(GetCurrentProcessId()));
            std::filesystem::create_directories(output);
            for (auto entity : registry.view<MeshComponent, TransformComponent>())
            {
                Editor_SelectEntity(context, entity);
                break;
            }
            stage = 1;
        }
        else if (stage == 1)
        {
            CheckDebugLayer(renderer);
            std::cout << "EDITOR_PREVIEW_PASS " << AssetPaths::ToUtf8(output / "editor.bmp") << std::endl;
            PostQuitMessage(0);
            stage = 10;
        }
        return;
    }
    if (stage == 0)
    {
        if (++frames < 30)
        {
            return;
        }
        Check(context.viewport.visible, "Viewport is not visible at startup");
        for (const char* name : {"Scene Hierarchy###SceneHierarchy", "Inspector###Inspector", "Viewport###Viewport", "Asset Browser###AssetBrowser", "Console###Console"})
        {
            const auto* window = ImGui::FindWindowByName(name);
            Check(window && window->DockId != 0, "Default panel was not docked");
        }
        uint32_t pixelX = 0;
        uint32_t pixelY = 0;
        const auto& viewport = context.viewport;
        Check(viewport.ScreenToPixel(ImVec2((viewport.imageMinimum.x + viewport.imageMaximum.x) * 0.5f,
            (viewport.imageMinimum.y + viewport.imageMaximum.y) * 0.5f), pixelX, pixelY), "Viewport center mapping failed");
        Check(pixelX == viewport.width / 2 && pixelY == viewport.height / 2, "Viewport pixel scaling failed");
        Check(!viewport.ScreenToPixel(ImVec2(viewport.imageMinimum.x - 1, viewport.imageMinimum.y), pixelX, pixelY), "Outside viewport accepted");
        output = engine.GetAssetRoot() / "ValidationOutput";
        std::filesystem::create_directories(output);
        expected = Find(registry, "Ближняя сфера");
        Pick(context, 0.5f, 0.5f);
        stage = 1;
        frames = 0;
    }
    else if (stage == 1)
    {
        if (context.viewport.pendingPick)
        {
            return;
        }
        Check(context.selected == expected, "Nearest visible mesh picking failed");
        std::cout << "EDITOR_PICK_NEAREST_PASS" << std::endl;
        PickWorld(context, glm::vec3(-4.5f, 0, 1));
        expected = Find(registry, "Submeshes");
        stage = 20;
    }
    else if (stage == 20)
    {
        if (context.viewport.pendingPick)
        {
            return;
        }
        Check(context.selected == expected, "First submesh did not select its owner");
        PickWorld(context, glm::vec3(-3.5f, 0, 1));
        stage = 21;
    }
    else if (stage == 21)
    {
        if (context.viewport.pendingPick)
        {
            return;
        }
        Check(context.selected == expected, "Second submesh did not select its owner");
        PickWorld(context, glm::vec3(1.8f, 1.8f, 0));
        stage = 22;
    }
    else if (stage == 22)
    {
        if (context.viewport.pendingPick)
        {
            return;
        }
        Check(context.selected == entt::null, "Sphere bounding-box corner was incorrectly selected");
        Pick(context, 0.5f, 0.9f);
        expected = Find(registry, "Terrain");
        stage = 23;
    }
    else if (stage == 23)
    {
        if (context.viewport.pendingPick)
        {
            return;
        }
        Check(context.selected == expected, "Displaced terrain picking failed");
        std::cout << "EDITOR_PICK_SUBMESH_TRIANGLE_TERRAIN_PASS" << std::endl;
        Pick(context, 0.02f, 0.02f);
        stage = 2;
    }
    else if (stage == 2)
    {
        if (context.viewport.pendingPick)
        {
            return;
        }
        Check(context.selected == entt::null, "Empty viewport did not clear selection");
        Pick(context, 0.5f, 0.5f);
        Editor_SelectEntity(context, Find(registry, "Дальняя сфера"));
        expected = context.selected;
        stage = 3;
        frames = 0;
    }
    else if (stage == 3)
    {
        if (++frames < 8)
        {
            return;
        }
        Check(context.selected == expected, "Old pick overwrote Hierarchy selection");
        Pick(context, 0.5f, 0.5f);
        stage = 4;
        frames = 0;
    }
    else if (stage == 4)
    {
        if (++frames < 8)
        {
            return;
        }
        Check(context.selected == expected, "Deleted entity pick changed selection");
        context.showViewport = false;
        stage = 5;
        frames = 0;
    }
    else if (stage == 5)
    {
        if (++frames < 3)
        {
            return;
        }
        Check(!context.viewport.visible, "Closed viewport still renders");
        context.showViewport = true;
        context.showInspector = false;
        EditorLayout::Save(context);
        context.showInspector = true;
        EditorLayout::Reset(context);
        stage = 6;
        frames = 0;
    }
    else if (stage == 6)
    {
        // Exercise the renderer's resize retirement while normal frames keep running.
        renderer.ResizeEditorViewport(350 + frames * 3, 220 + frames);
        if (++frames < 25)
        {
            return;
        }
        std::string error;
        const auto path = AssetPaths::ToUtf8(output / L"Сцена-проверка.json");
        Check(engine.SaveScene(path, error), "Cyrillic scene save failed");
        Pick(context, 0.5f, 0.5f);
        Check(engine.LoadScene(path, error), "Cyrillic scene reload failed");
        stage = 7;
        frames = 0;
    }
    else if (stage == 7)
    {
        if (++frames < 15)
        {
            return;
        }
        Check(context.selected == entt::null, "Old-scene pick changed selection");
        Check(Find(registry, "Дальняя сфера") != entt::null, "Cyrillic name did not round trip");
        Check(EditorPlayWindow::Start(context), "Separate Play window failed to start");
        stage = 8;
        frames = 0;
    }
    else if (stage == 8)
    {
        if (++frames < 90)
        {
            return;
        }
        Check(EditorPlayWindow::IsRunning(), "Separate Play window closed unexpectedly");
        EditorPlayWindow::Stop(context);
        stage = 9;
        frames = 0;
    }
    else if (stage == 9)
    {
        if (++frames < 30 || EditorPlayWindow::IsRunning())
        {
            return;
        }
        CheckDebugLayer(renderer);
        std::cout << "EDITOR_SMOKE_PASS: docking, viewport, MRT depth picking, empty/background, stale selection/deletion/scene, resize retirement, Cyrillic, separate Play; layout=" << EditorLayout::SettingsDirectory() << std::endl;
        PostQuitMessage(0);
        stage = 10;
    }
}

void EditorValidation::AfterOverlay(D3DRenderAdapter& renderer)
{
    const bool captureRenderStateEditor = renderStateTest && renderer.mEditorScene && stage == 12 && frames >= 3;
    if (renderStateTest && stage != 1 && !captureRenderStateEditor)
    {
        return;
    }
    if (stage == 4 && frames == 0)
    {
        auto& registry = Editor_GetContext()->engine->GetRegistry();
        registry.destroy(Find(registry, "Ближняя сфера"));
    }
    const int captureStage = materialTest ? 31 : (preview ? 1 : 20);
    const bool captureMaterialEditor = materialTest && stage == 39 && frames >= 2 && Editor_GetContext()->materialPreviewMesh;
    const bool captureEditor = captureMaterialEditor || captureRenderStateEditor;
    if ((!renderStateTest && stage != captureStage && !captureMaterialEditor) || capture || output.empty() ||
        ((materialTest || renderStateTest) && std::filesystem::is_regular_file(output / (captureEditor ? "material-editor.bmp" : "editor.bmp"))))
    {
        return;
    }
    const auto bindings = renderer.GetImGuiBindings();
    const bool captureScene = renderStateTest && !captureRenderStateEditor;
    ID3D12Resource* sourceResource = captureScene && renderer.mEditorScene
        ? renderer.mEditorTarget->color.Get() : renderer.CurrentBackBuffer();
    const auto sourceState = captureScene && renderer.mEditorScene
        ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    const auto description = sourceResource->GetDesc();
    UINT64 bytes = 0;
    bindings.Device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    ThrowIfFailed(bindings.Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&capture)));
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(sourceResource, sourceState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    bindings.CommandList->ResourceBarrier(1, &barrier);
    CD3DX12_TEXTURE_COPY_LOCATION source(sourceResource, 0);
    CD3DX12_TEXTURE_COPY_LOCATION destination(capture.Get(), footprint);
    bindings.CommandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(sourceResource, D3D12_RESOURCE_STATE_COPY_SOURCE, sourceState);
    bindings.CommandList->ResourceBarrier(1, &barrier);
}

void EditorValidation::CaptureCompleted(D3DRenderAdapter& renderer)
{
    if (!capture)
    {
        return;
    }
    // Only the opt-in validation capture waits. Runtime picking never does.
    renderer.FlushCommandQueue();
    void* data = nullptr;
    ThrowIfFailed(capture->Map(0, nullptr, &data));
    const uint32_t width = footprint.Footprint.Width;
    const uint32_t height = footprint.Footprint.Height;
    BITMAPFILEHEADER fileHeader = {};
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + width * height * 4;
    BITMAPINFOHEADER imageHeader = {};
    imageHeader.biSize = sizeof(imageHeader);
    imageHeader.biWidth = width;
    imageHeader.biHeight = -LONG(height);
    imageHeader.biPlanes = 1;
    imageHeader.biBitCount = 32;
    const char* filename = (materialTest && stage == 39) || (renderStateTest && stage == 12)
        ? "material-editor.bmp" : "editor.bmp";
    std::ofstream file(output / filename, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char*>(&imageHeader), sizeof(imageHeader));
    std::vector<uint8_t> row(width * 4);
    for (uint32_t y = 0; y < height; ++y)
    {
        std::memcpy(row.data(), static_cast<uint8_t*>(data) + footprint.Offset + y * footprint.Footprint.RowPitch, row.size());
        for (uint32_t x = 0; x < width; ++x)
        {
            std::swap(row[x * 4], row[x * 4 + 2]);
        }
        file.write(reinterpret_cast<const char*>(row.data()), row.size());
    }
    file.close();
    if (renderStateTest && stage == 1)
    {
        CheckRenderStatePixels(static_cast<const uint8_t*>(data) + footprint.Offset,
            footprint.Footprint.RowPitch, width, height);
    }
    capture->Unmap(0, nullptr);
    capture.Reset();
}


void EditorValidation::CheckMaterials(Engine& engine, D3DRenderAdapter& renderer)
{
    auto& registry = engine.GetRegistry();
    auto& resources = engine.GetResources();
    auto& context = *Editor_GetContext();
    const auto meshFor = [&](const char* name)
    {
        return registry.get<MeshComponent>(Find(registry, name)).meshID;
    };
    const auto materialFor = [&](const char* name) -> Material&
    {
        const auto& mesh = resources.GetMesh(meshFor(name));
        Check(!mesh.submeshes.empty(), "Material demo mesh did not load");
        return resources.GetMaterial(mesh.submeshes.front().material);
    };
    const auto pick = [&](const glm::vec3& position)
    {
        const float distance = position.z + 14;
        const float tangent = std::tan(1.05f * 0.5f);
        const float aspect = float(context.viewport.width) / context.viewport.height;
        Pick(context, 0.5f + position.x / (2 * distance * tangent * aspect),
            0.5f - (position.y - 2) / (2 * distance * tangent));
    };
    const auto stats = renderer.GetSurfaceCacheStats();

    switch (stage)
    {
    case 0:
    {
        if (++frames < 90)
        {
            return;
        }
        Check(stats.created == 4, "Expected two default PSOs and one each for masked and Unlit");
        output = std::filesystem::temp_directory_path() / ("LSMaterialCheck-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(output);
        stage = 31;
        if (!engine.IsStandalone())
        {
            Check(context.viewport.visible, "Material editor viewport is hidden");
            pick(glm::vec3(0, 2, 0));
        }
        break;
    }
    case 31:
    {
        if (engine.IsStandalone())
        {
            CheckDebugLayer(renderer);
            std::cout << "MATERIAL_STANDALONE_PASS " << AssetPaths::ToUtf8(output / "editor.bmp") << std::endl;
            PostQuitMessage(0);
            stage = 40;
            return;
        }
        if (context.viewport.pendingPick)
        {
            return;
        }
        Check(context.selected == Find(registry, "Behind grille"), "Mask hole blocked the entity behind the grille");
        // The grid bars are at texels 3..6 of each 16 texel tile.
        pick(glm::vec3(4.8f * (36.5f / 64 - 0.5f), 2, 0));
        stage = 32;
        break;
    }
    case 32:
    {
        if (context.viewport.pendingPick)
        {
            return;
        }
        Check(context.selected == Find(registry, "Masked grille"), "Opaque mask pixel did not select the grille");
        Material& material = materialFor("Rough dielectric");
        material.parameters.roughness = 0.31f;
        material.parameters.normal = materialFor("ORM and normal").parameters.normal;
        material.parameters.albedo = materialFor("Masked grille").parameters.albedo;
        materialStats = stats;
        stage = 33;
        break;
    }
    case 33:
    {
        Check(stats.created == materialStats.created && stats.misses == materialStats.misses &&
            stats.hits > materialStats.hits, "Texture/constant edit missed the existing PSO cache");
        auto base = std::make_shared<MaterialBase>();
        base->mode = SurfaceMode::Masked;
        materialFor("Rough dielectric").base = base;
        materialStats = stats;
        stage = 34;
        break;
    }
    case 34:
    {
        Check(stats.created == materialStats.created + 1, "New masked state should create exactly one PSO");
        const auto shaderPath = output / "ReloadProbe.hlsl";
        const auto sourceDirectory = std::filesystem::absolute("Shaders");
        for (const char* include : {"Surface.hlsli", "SurfaceOutput.hlsli", "LightingUtil.hlsl"})
        {
            std::filesystem::copy_file(sourceDirectory / include, output / include,
                std::filesystem::copy_options::overwrite_existing);
        }
        std::filesystem::copy_file(sourceDirectory / "Unlit.hlsl", shaderPath,
            std::filesystem::copy_options::overwrite_existing);
        auto base = std::make_shared<MaterialBase>();
        base->program.file = shaderPath.wstring();
        materialFor("Rough dielectric").base = base;
        materialStats = stats;
        stage = 35;
        break;
    }
    case 35:
    {
        Check(stats.created == materialStats.created + 1, "A different shader program should create exactly one PSO");
        std::ofstream(output / "ReloadProbe.hlsl") << "not a valid HLSL program\n";
        Check(!renderer.ReloadShaders(), "Broken HLSL was accepted by reload");
        Check(renderer.GetSurfaceDiagnostic().find("ReloadProbe.hlsl") != std::string::npos,
            "Reload diagnostic did not identify the broken program");
        materialStats = renderer.GetSurfaceCacheStats();
        stage = 36;
        break;
    }
    case 36:
    {
        Check(stats.created == materialStats.created && stats.hits > materialStats.hits,
            "Failed reload did not keep the working pipeline");
        MaterialDesc broken;
        broken.name = "Missing map and shader";
        broken.albedoTexture = (output / "MissingMap.png").wstring();
        broken.surface.emplace().program.file = (output / "MissingShader.hlsl").wstring();
        resources.SetMeshMaterial(meshFor("Rough dielectric"), resources.CreateTexturedMaterial(broken));
        stage = 37;
        frames = 0;
        break;
    }
    case 37:
    {
        if (++frames < 60)
        {
            return;
        }
        const auto& parameters = materialFor("Rough dielectric").parameters;
        Check(resources.GetTexture(parameters.albedo).state == ResourceState::Failed,
            "A missing explicit map must retain Failed state");
        const auto materialId = resources.GetMesh(meshFor("Rough dielectric")).submeshes.front().material;
        const auto& gpu = *renderer.GetOrLoadMaterial(materialId);
        Check(gpu.DiffuseSrvHeapIndex == renderer.mTextures.at(std::to_string(resources.GetPlaceholderTexture()))->alternateSrvIndex,
            "Missing color map did not bind the checker in sRGB");
        MaterialDesc description;
        description.name = "Preview material";
        description.color = {0.25f, 0.48f, 0.85f};
        description.roughness = 0.29f;
        description.surface.emplace().defaults.metallic = 0.27f;
        description.surface->defaults.uvOffset = {0.1f, 0.2f};
        description.surface->defaults.baseColor.a = 0.91f;
        const std::string path = AssetPaths::ToUtf8(output / L"Материал.lsmaterial");
        resources.SaveMaterialAsset(path, description);
        const MaterialID asset = resources.LoadMaterialAsset(path);
        Check(resources.LoadMaterialAsset(path) == asset, "Repeated file assignment created another material");
        const auto& restored = resources.GetMaterial(asset).parameters;
        Check(restored.baseColor == glm::vec4(description.color, 0.91f) &&
            restored.metallic == 0.27f && restored.uvOffset == glm::vec2(0.1f, 0.2f),
            "Material file round trip lost parameters");
        description.roughness = 0.35f;
        resources.SaveMaterialAsset(path, description);
        Check(resources.LoadMaterialAsset(path) == asset && resources.GetMaterial(asset).parameters.roughness == 0.35f,
            "Saving an assigned material did not update existing references");

        const auto grille = Find(registry, "Masked grille");
        const MeshID mesh = meshFor("Masked grille");
        const MaterialID original = resources.GetMesh(mesh).submeshes.front().material;
        registry.emplace<MaterialComponent>(grille, MaterialComponent{path, asset, {}});
        const auto snapshot = output / "MaterialComponentScene.json";
        SceneSerializer::Save(engine.GetWorld(), AssetPaths::ToUtf8(snapshot), engine.GetAssetRoot());
        const auto roundTrip = SceneSerializer::Read(AssetPaths::ToUtf8(snapshot));
        bool restoredComponent = false;
        World testWorld;
        for (const auto& entity : roundTrip.entities)
        {
            if (entity.tag == "Masked grille")
            {
                SceneSerializer::CreateEntity(testWorld, resources, entity);
                const auto restoredEntity = Find(testWorld.registry, "Masked grille");
                const auto& component = testWorld.registry.get<MaterialComponent>(restoredEntity);
                restoredComponent = component.assetPath == path && component.material == asset;
                break;
            }
        }
        Check(restoredComponent, "Material component did not survive scene save and load");
        Check(resources.GetMesh(mesh).submeshes.front().material == original,
            "Component assignment altered a shared mesh");

        pick(glm::vec3(0, 2, 0));
        stage = 38;
        break;
    }
    case 38:
    {
        if (context.viewport.pendingPick)
        {
            return;
        }
        const auto grille = Find(registry, "Masked grille");
        Check(context.selected == grille, "Scene draw ignored the entity's opaque material component");
        registry.remove<MaterialComponent>(grille);
        Editor_OpenMaterial(context, AssetPaths::ToUtf8(output / L"Материал.lsmaterial"));
        stage = 39;
        frames = 0;
        pick(glm::vec3(0, 2, 0));
        break;
    }
    case 39:
    {
        if (++frames < 3 || context.viewport.pendingPick || !context.materialPreviewMesh)
        {
            return;
        }
        Check(context.selected == Find(registry, "Behind grille"), "Removing the component did not restore the mask");
        Check(std::filesystem::is_regular_file(output / "material-editor.bmp"), "Material editor preview was not captured");
        CheckDebugLayer(renderer);
        std::cout << "MATERIAL_SMOKE_PASS created=" << stats.created << " hits=" << stats.hits
            << " misses=" << stats.misses << " capture=" << AssetPaths::ToUtf8(output / "editor.bmp") << std::endl;
        std::cout << "MATERIAL_EDITOR_PASS " << AssetPaths::ToUtf8(output / "material-editor.bmp") << std::endl;
        PostQuitMessage(0);
        stage = 40;
        break;
    }
    }
}

void EditorValidation::CheckRenderStatePixels(const uint8_t* pixels, uint32_t rowPitch, uint32_t width, uint32_t height)
{
    const auto sample = [&](const glm::vec3& position)
    {
        const float distance = position.z + 18.0f;
        const float tangent = std::tan(1.05f * 0.5f);
        const float aspect = float(width) / height;
        const int pixelX = std::clamp(int((0.5f + position.x / (2 * distance * tangent * aspect)) * width), 0, int(width) - 1);
        const int pixelY = std::clamp(int((0.5f - position.y / (2 * distance * tangent)) * height), 0, int(height) - 1);
        const auto* color = pixels + pixelY * rowPitch + pixelX * 4;
        return glm::ivec3(color[0], color[1], color[2]);
    };
    const auto orange = sample({-4.75f, 4.65f, -1.5f});
    Check(orange.r > orange.b + 80, "Opaque near orange did not occlude blue");
    const auto cyan = sample({0, -0.4f, 1});
    Check(cyan.g > cyan.r + 80 && cyan.b > cyan.r + 80, "Depth Test Off did not show cyan behind gray");
    const auto writeOn = sample({5.0f, -0.4f, -1});
    const auto writeOff = sample({-5.0f, -5.0f, -1});
    Check(writeOn.r > writeOn.b + 80, "Depth Write On failed to occlude the later far sphere");
    Check(writeOff.b > writeOff.r + 80, "Depth Write Off still occluded the later far sphere");
    const auto greater = sample({0, -5.6f, 0});
    Check(greater.g > greater.r + 80, "Greater depth comparison did not show the occluded green surface");
    const auto unconditional = sample({5.2f, -5.6f, 0});
    Check(unconditional.b > unconditional.r + 80, "Test Off + Write On did not unconditionally replace near depth");
    const auto additive = sample({-5.2f, -0.05f, 0});
    Check(additive.r > 90 && additive.g > 90, "Additive overlap did not accumulate red and green");
    const auto alpha = sample({4.9f, 4.8f, -1});
    // Encoded gray, then straight-alpha red, green, blue at alpha 0.5; allow UNORM rounding.
    Check(std::abs(alpha.r - 83) <= 5 && std::abs(alpha.g - 138) <= 5 && std::abs(alpha.b - 174) <= 5,
        "Alpha layers did not blend back to front over their opaque backdrop");
    const auto foreground = sample({6.1f, 4.0f, -2});
    Check(foreground.r > 200 && foreground.g > 180 && foreground.b < 100,
        "Transparent layers ignored the opaque foreground depth");
    const auto hole = sample({0.3f, 4.5f, -1});
    const auto bar = sample({0, 4.8f, -1});
    Check(hole.b > hole.r + 80 && bar.r > 200 && bar.g > 200 && bar.b > 200,
        "Masked clip did not preserve holes and opaque bars");
    std::cout << "RENDER_STATE_GPU_PIXELS_PASS " << width << "x" << height << std::endl;
}

void EditorValidation::CheckRenderStates(Engine& engine, D3DRenderAdapter& renderer)
{
    auto& resources = engine.GetResources();
    auto& registry = engine.GetRegistry();
    auto& context = *Editor_GetContext();
    const auto pick = [&](const glm::vec3& position, const char* name)
    {
        const float distance = position.z + 18;
        const float tangent = std::tan(1.05f * 0.5f);
        const float aspect = float(context.viewport.width) / context.viewport.height;
        expected = Find(registry, name);
        Pick(context, 0.5f + position.x / (2 * distance * tangent * aspect),
            0.5f - position.y / (2 * distance * tangent));
    };
    if (stage == 0)
    {
        if (++frames < 120 || resources.HasPendingLoads())
        {
            return;
        }
        output = std::filesystem::temp_directory_path() / ("LSRenderStates-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(output);
        Check(renderer.GetSurfaceDiagnostic().empty(), "Demo material pipeline failed");
        stage = 1;
        return;
    }
    if (stage == 1)
    {
        Check(std::filesystem::is_regular_file(output / "editor.bmp"), "Render-state scene capture missing");
        if (!engine.IsStandalone())
        {
            pick({-4.75f, 4.65f, -1.5f}, "01 Near orange cube");
            stage = 2;
            return;
        }
        stage = 10;
    }
    if (stage >= 2 && stage <= 6)
    {
        if (context.viewport.pendingPick)
        {
            return;
        }
        Check(context.selected == expected, "Render-state demo picking selected the wrong entity");
        if (stage == 2)
        {
            pick({0.3f, 4.5f, -1}, "02 Blue behind grille");
        }
        else if (stage == 3)
        {
            pick({0, 4.8f, -1}, "02 Masked grille");
        }
        else if (stage == 4)
        {
            pick({4.9f, 4.8f, -1}, "03 AlphaBlue");
        }
        else if (stage == 5)
        {
            pick({6.1f, 4.0f, -2}, "03 Opaque foreground cube");
        }
        else
        {
            std::cout << "RENDER_STATE_PICK_OPAQUE_MASK_ALPHA_PASS" << std::endl;
            stage = 10;
        }
        if (stage < 10)
        {
            ++stage;
        }
        return;
    }
    if (stage == 10)
    {
        const auto entity = Find(registry, "01 Near orange cube");
        renderStateMaterial = registry.get<MaterialComponent>(entity).material;
        originalMaterial = SceneSerializer::ReadMaterialAsset(resources.ResolveAssetPath("Assets/MaterialRenderStatesDemo/Orange.lsmaterial"));
        MaterialDesc probe = originalMaterial;
        probe.roughness = 0.2f;
        probe.albedoTexture = L"Assets/MaterialRenderStatesDemo/Mask.png";
        const auto shaderDirectory = std::filesystem::absolute("Shaders");
        for (const char* include : {"Surface.hlsli", "SurfaceOutput.hlsli", "LightingUtil.hlsl", "Unlit.hlsl"})
        {
            std::filesystem::copy_file(shaderDirectory / include, output / include,
                std::filesystem::copy_options::overwrite_existing);
        }
        probe.surface->program.file = (output / "Unlit.hlsl").wstring();
        resources.UpdateTexturedMaterial(renderStateMaterial, probe);
        const auto append = [&](const MaterialRenderState& state)
        {
            if (std::find(renderStates.begin(), renderStates.end(), state) == renderStates.end())
            {
                renderStates.push_back(state);
            }
        };
        append({});
        MaterialRenderState state;
        state.depthTest = false;
        append(state);
        state.depthWrite = false;
        append(state);
        state = {};
        state.depthWrite = false;
        append(state);
        for (size_t index = 0; index < DepthComparisonNames.size(); ++index)
        {
            state = {};
            state.depthCompare = static_cast<DepthComparison>(index);
            append(state);
        }
        append(MaterialRenderState::AlphaBlend());
        append(MaterialRenderState::Additive());
        for (size_t index = 0; index < BlendFactorNames.size(); ++index)
        {
            state = MaterialRenderState::AlphaBlend();
            state.sourceColor = static_cast<BlendFactor>(index);
            append(state);
            state = MaterialRenderState::AlphaBlend();
            state.destinationColor = static_cast<BlendFactor>(index);
            append(state);
            state = MaterialRenderState::AlphaBlend();
            state.sourceAlpha = static_cast<BlendFactor>(index);
            append(state);
            state = MaterialRenderState::AlphaBlend();
            state.destinationAlpha = static_cast<BlendFactor>(index);
            append(state);
        }
        for (size_t index = 0; index < BlendOperationNames.size(); ++index)
        {
            state = MaterialRenderState::AlphaBlend();
            state.colorOperation = static_cast<BlendOperation>(index);
            append(state);
            state = MaterialRenderState::AlphaBlend();
            state.alphaOperation = static_cast<BlendOperation>(index);
            append(state);
        }
        expectedPipelines = renderer.GetSurfaceCacheStats().created;
        renderStateIndex = 0;
        frames = 0;
        stage = 11;
        // The first probe is drawn after this Tick.
        resources.SetMaterialRenderState(renderStateMaterial, renderStates.front());
        ++expectedPipelines;
        return;
    }
    if (stage == 11)
    {
        Check(renderer.GetSurfaceCacheStats().created == expectedPipelines,
            "Changing one state field did not create exactly one PSO, or returning to a cached state created another");
        Check(renderer.GetSurfaceDiagnostic().empty(), "Render-state combination failed D3D12 pipeline validation");
        const auto& state = renderStates[renderStateIndex % renderStates.size()];
        MaterialDesc description = originalMaterial;
        description.surface->renderState = state;
        const auto path = AssetPaths::ToUtf8(output / "RoundTrip.lsmaterial");
        resources.SaveMaterialAsset(path, description);
        const MaterialID asset = resources.LoadMaterialAsset(path);
        Check(SceneSerializer::ReadMaterialAsset(AssetPaths::FromUtf8(path)).surface->renderState == state,
            "Render-state material file round trip lost fields");
        description.roughness = 0.37f;
        resources.SaveMaterialAsset(path, description);
        Check(resources.LoadMaterialAsset(path) == asset && resources.GetMaterial(asset).base->renderState == state &&
            resources.GetMaterial(asset).parameters.roughness == 0.37f, "Live file save did not update the cached material");
        ++renderStateIndex;
        if (renderStateIndex < renderStates.size() * 2)
        {
            resources.SetMaterialRenderState(renderStateMaterial, renderStates[renderStateIndex % renderStates.size()]);
            Check(resources.GetMaterial(renderStateMaterial).parameters.roughness == 0.2f &&
                resources.GetMaterial(renderStateMaterial).parameters.albedo != 0,
                "Render-state API discarded constants or texture bindings");
            if (renderStateIndex < renderStates.size())
            {
                ++expectedPipelines;
            }
            resources.GetMaterial(renderStateMaterial).parameters.roughness = 0.2f;
            return;
        }
        resources.UpdateTexturedMaterial(renderStateMaterial, originalMaterial);
        World roundTripWorld;
        SceneSerializer::EntityDescription entity;
        entity.tag = "Inline render state";
        entity.transform = {{0, 0, 0}, {0, 0, 0}, {1, 1, 1}};
        entity.mesh.emplace();
        entity.mesh->source = "primitive";
        entity.mesh->primitive = "cube";
        entity.mesh->material = description;
        entity.renderOrder = RenderOrderComponent{17};
        SceneSerializer::CreateEntity(roundTripWorld, resources, entity);
        const auto scenePath = AssetPaths::ToUtf8(output / "Inline.json");
        SceneSerializer::Save(roundTripWorld, scenePath, engine.GetAssetRoot());
        const auto restored = SceneSerializer::Read(scenePath);
        Check(restored.entities.front().mesh->material->surface->renderState == state &&
            restored.entities.front().renderOrder->layer == 17, "Inline state or submission order did not survive scene save/load");
        const auto invalidPath = output / "Invalid.lsmaterial";
        for (const char* field : {"depthCompare", "sourceColor", "destinationColor", "colorOperation", "sourceAlpha", "destinationAlpha", "alphaOperation"})
        {
            std::ofstream file(invalidPath);
            file << "{\"version\":1,\"material\":{\"surface\":{\"renderState\":{\"" << field << "\":\"InvalidValue\"}}}}";
            file.close();
            bool rejected = false;
            try
            {
                SceneSerializer::ReadMaterialAsset(invalidPath);
            }
            catch (const std::exception& error)
            {
                const std::string diagnostic = error.what();
                rejected = diagnostic.find("Invalid.lsmaterial") != std::string::npos && diagnostic.find(field) != std::string::npos;
            }
            Check(rejected, "Invalid enum diagnostic omitted its file or field");
        }
        MaterialRenderState invalid;
        invalid.sourceAlpha = static_cast<BlendFactor>(999);
        bool rejected = false;
        try
        {
            resources.SetMaterialRenderState(renderStateMaterial, invalid);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Check(rejected && resources.GetMaterial(renderStateMaterial).base->renderState == originalMaterial.surface->renderState,
            "Invalid API state was accepted or mutated the material");
        CheckDebugLayer(renderer);
        std::cout << "RENDER_STATE_CACHE_ROUNDTRIP_LIVE_PASS unique=" << renderStates.size()
            << " passes=2 created=" << expectedPipelines << std::endl;
        if (!engine.IsStandalone())
        {
            Editor_OpenMaterial(context, "Assets/MaterialRenderStatesDemo/AlphaBlue.lsmaterial");
        }
        stage = 12;
        frames = 0;
        return;
    }
    if (stage == 12 && ++frames >= 10)
    {
        CheckDebugLayer(renderer);
        ComPtr<ID3D12InfoQueue> debugQueue;
        const bool debugLayerAvailable = SUCCEEDED(renderer.GetImGuiBindings().Device->QueryInterface(IID_PPV_ARGS(&debugQueue)));
        std::cout << "RENDER_STATE_D3D12_VALIDATION " << (debugLayerAvailable ? "available, errors=0" : "unavailable") << std::endl;
        if (!engine.IsStandalone())
        {
            Check(context.materialPreviewMesh != 0, "Material Editor preview did not draw the blended material");
            Check(std::filesystem::is_regular_file(output / "material-editor.bmp"), "Render State editor preview capture missing");
            std::cout << "RENDER_STATE_MATERIAL_EDITOR_PASS " << AssetPaths::ToUtf8(output / "material-editor.bmp") << std::endl;
        }
        std::cout << (engine.IsStandalone() ? "RENDER_STATES_STANDALONE_PASS " : "RENDER_STATES_EDITOR_PASS ")
            << AssetPaths::ToUtf8(output / "editor.bmp") << std::endl;
        PostQuitMessage(0);
        stage = 13;
    }
}
