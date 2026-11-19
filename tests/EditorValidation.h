#pragma once
#include <D3DRenderAdapter.h>
#include <filesystem>

class Engine;

class EditorValidation
{
public:
    bool preview = false;
    bool materialTest = false;
    bool renderStateTest = false;
    void Tick(Engine& engine, D3DRenderAdapter& renderer);
    void AfterOverlay(D3DRenderAdapter& renderer);

private:
    int stage = 0;
    int frames = 0;
    int totalFrames = 0;
    entt::entity expected = entt::null;
    std::filesystem::path output;
    ComPtr<ID3D12Resource> capture;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    void CheckMaterials(Engine& engine, D3DRenderAdapter& renderer);
    void CheckRenderStates(Engine& engine, D3DRenderAdapter& renderer);
    void CheckRenderStatePixels(const uint8_t* pixels, uint32_t rowPitch, uint32_t width, uint32_t height);
    std::vector<MaterialRenderState> renderStates;
    size_t renderStateIndex = 0;
    uint64_t expectedPipelines = 0;
    MaterialID renderStateMaterial = 0;
    MaterialDesc originalMaterial;
    D3DRenderAdapter::SurfaceCacheStats materialStats;
    void CaptureCompleted(D3DRenderAdapter& renderer);
};
