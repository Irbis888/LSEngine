#include "EditorValidation.h"
#include <Engine.h>
#include <AssetPaths.h>
#include <TerrainSystem.h>
#include <ResourceManager.h>
#include <Editor/EditorContext.h>
#include <Editor/EditorLayout.h>
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
    if (stage == 4 && frames == 0)
    {
        auto& registry = Editor_GetContext()->engine->GetRegistry();
        registry.destroy(Find(registry, "Ближняя сфера"));
    }
    const int captureStage = materialTest ? 31 : (preview ? 1 : 20);
    if (stage != captureStage || capture || output.empty() ||
        (materialTest && std::filesystem::is_regular_file(output / "editor.bmp")))
    {
        return;
    }
    const auto bindings = renderer.GetImGuiBindings();
    const auto description = renderer.CurrentBackBuffer()->GetDesc();
    UINT64 bytes = 0;
    bindings.Device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    ThrowIfFailed(bindings.Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&capture)));
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderer.CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    bindings.CommandList->ResourceBarrier(1, &barrier);
    CD3DX12_TEXTURE_COPY_LOCATION source(renderer.CurrentBackBuffer(), 0);
    CD3DX12_TEXTURE_COPY_LOCATION destination(capture.Get(), footprint);
    bindings.CommandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderer.CurrentBackBuffer(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
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
    std::ofstream file(output / "editor.bmp", std::ios::binary);
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
        CheckDebugLayer(renderer);
        std::cout << "MATERIAL_SMOKE_PASS created=" << stats.created << " hits=" << stats.hits
            << " misses=" << stats.misses << " capture=" << AssetPaths::ToUtf8(output / "editor.bmp") << std::endl;
        PostQuitMessage(0);
        stage = 40;
        break;
    }
    }
}
