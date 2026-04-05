#pragma once
#include "Commons.h"
#include "d3dUtils.h"
#include "MathHelper.h"

using Microsoft::WRL::ComPtr;

class D3DRenderAdapter : public IRenderAdapter
{
public:
    void Init(void* windowHandle, uint32_t width, uint32_t height) override;
    void BeginFrame() override;
    void EndFrame() override;

    void SetTransform(const TransformComponent& world) override;

    void SetMaterial(MaterialID material) override;

    void DrawIndexed(
        uint32_t indexCount,
        uint32_t startIndex,
        int32_t baseVertex
    ) override;

    // Draw a complete mesh with all its submeshes, binding appropriate material for each
    void DrawMesh(MeshID meshId);

    // Draw a specific submesh from a mesh
    void DrawSubmesh(MeshID meshId, uint32_t submeshIndex);

    //void InitDirect3D();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRtvAndDsvDescriptorHeaps();


    ID3D12Resource* CurrentBackBuffer()const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()const;

    void OnResize();
	void FlushCommandQueue();

private:
    HWND      mhMainWnd = nullptr;
    int mClientWidth;
    int mClientHeight;

    ComPtr<ID3D12Device> md3dDevice;
    ComPtr<IDXGIFactory4> mdxgiFactory;

    ComPtr<ID3D12CommandQueue> mCommandQueue;
    ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
    ComPtr<ID3D12GraphicsCommandList> mCommandList;

    ComPtr<ID3D12Fence> mFence;
    UINT64 mCurrentFence = 0;

    ComPtr<IDXGISwapChain> mSwapChain;

    ComPtr<ID3D12Resource> mSwapChainBuffer[2];
    int mCurrBackBuffer = 0;

    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;

    ComPtr<ID3D12Resource> mDepthStencilBuffer;
    D3D12_VIEWPORT mScreenViewport;
    D3D12_RECT mScissorRect;

    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    // Descriptor heap for CBV/SRV/UAV
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mCbvSrvUavHeap;
    UINT mCbvSrvUavDescriptorCount = 0;

    // Simple ownership of created GPU resources to keep them alive
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> mOwnedResources;

    // Next free descriptor index in the CBV/SRV/UAV heap
    UINT mNextCbvSrvIndex = 0;

    // Current material/transform state (set by SetMaterial/SetTransform)
    MaterialID mCurrentMaterial = 0;
    TransformComponent mCurrentTransform;

public:
    // Create SRV from an already loaded ID3D12Resource (returns descriptor index)
    int CreateSRV(ID3D12Resource* resource);

    // Load texture from file and create SRV (returns descriptor index)
    // NOTE: CreateSRVFromFile implementation removed to avoid linking DDSTextureLoader from this module.

    // Create a constant buffer view from CPU data (returns descriptor index)
    int CreateCBV(const void* data, UINT64 byteSize, ID3D12Resource** outUploadResource = nullptr);

    // Get GPU descriptor handle for a heap index
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(UINT index) const;

    D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    std::unordered_map<MeshID, std::unique_ptr<MeshGPU>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<MaterialGPU>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<TextureGPU>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
    std::unordered_map<std::string, ComPtr<ID3D12RootSignature>> mRootSignatures;

    // Input layouts for different vertex formats
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    std::unordered_map<std::string, std::unique_ptr<CustomBuffer>> mBuffers;
    std::unordered_map<std::string, std::vector<DXGI_FORMAT>> mBufferFormats;

    // Build root signatures and PSOs
    void BuildShadersAndInputLayout();
    void BuildRootSignatures();
    void BuildPSOs();
    std::vector<D3D12_STATIC_SAMPLER_DESC> GetStaticSamplers();

    // Mesh upload (lazy loading from ResourceManager)
    void SetResourceManager(class ResourceManager* resourceManager);
    MeshGPU* UploadMesh(MeshID meshId);
    MeshGPU* GetMeshGPU(MeshID meshId);

    bool      m4xMsaaState = false;    // 4X MSAA enabled
    UINT      m4xMsaaQuality = 0;

    class ResourceManager* mResourceManager = nullptr;
};
