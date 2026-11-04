#pragma once
#include "TerrainSystem.h"
#include <wrl/client.h>
#include <d3d12.h>
class Engine;
class D3DRenderAdapter;
// Opt-in validation through the ordinary Application frame loop and renderer.
class TerrainValidation
{
public:
    bool Tick(Engine&,const FrameContext&,D3DRenderAdapter&);
    void AfterDraw(D3DRenderAdapter&);
private:
    void Math(const std::filesystem::path&);
    int stage=0,frames=0,cycles=0;
    entt::entity terrain=entt::null;
    double maxFrame=0,totalFrames=0;
    size_t frameCount=0;
    double loadingMax=0,largeTotal=0,largeMax=0;
    size_t largeFrames=0,peakGpu=0;
    std::shared_ptr<const TerrainResource> previous;
    std::filesystem::path output;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    std::string captureName;
    bool capturedNormal=false,capturedWire=false;
};
