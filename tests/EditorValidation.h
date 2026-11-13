#pragma once
#include <D3DRenderAdapter.h>
#include <filesystem>

class Engine;

class EditorValidation
{
public:
    bool preview = false;
    bool materialTest = false;
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
    D3DRenderAdapter::SurfaceCacheStats materialStats;
    void CaptureCompleted(D3DRenderAdapter& renderer);
};
