#pragma once
#include "Commons.h"
#include "d3dUtils.h"
#include "MathHelper.h"
#include "FrameRes.h"
#include "ResourceManager.h"
#include <map>
#include <deque>
#include <unordered_set>

using Microsoft::WRL::ComPtr;
struct MeshGeometry;

struct Dx12ImGuiBindings
{
    ID3D12Device* Device = nullptr;
    ID3D12CommandQueue* CommandQueue = nullptr;
    ID3D12GraphicsCommandList* CommandList = nullptr;
    ID3D12DescriptorHeap* SrvHeap = nullptr;
    UINT SrvDescriptorSize = 0;
    DXGI_FORMAT RtvFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT DsvFormat = DXGI_FORMAT_UNKNOWN;
    int NumFramesInFlight = 2;
};

class D3DRenderAdapter : public IRenderAdapter
{
public:
    ~D3DRenderAdapter() override;
    void Init(void* windowHandle, uint32_t width, uint32_t height) override;
    void BeginFrame() override;
    void EndFrame() override;
    bool ReloadShaders() override;
    struct SurfaceCacheStats
    {
        uint64_t created = 0;
        uint64_t hits = 0;
        uint64_t misses = 0;
    };
    SurfaceCacheStats GetSurfaceCacheStats() const
    {
        return mSurfaceCacheStats;
    }
    const std::string& GetSurfaceDiagnostic() const
    {
        return mSurfaceDiagnostic;
    }
    uint64_t ResizeEditorViewport(uint32_t width, uint32_t height) override;
    void BeginScene(bool editor, bool visible) override;
    void EndScene() override;
    void SetRenderEntity(entt::entity entity) override;
    void RequestViewportPick(uint32_t x, uint32_t y, uint64_t request, uint64_t sceneGeneration) override;
    bool PollViewportPick(ViewportPickResult& result) override;


    void SetTransform(const TransformComponent& world) override;
    void SetCamera(const CameraComponent& camera, const TransformComponent& transform) override;
    void SetLights(const SceneLightData& lights) override;

    void SetMaterial(MaterialID material) override;
    void SetTimeData(float TotalTime, float DeltaTime) override;

    void DrawIndexed(
        uint32_t indexCount,
        uint32_t startIndex,
        int32_t baseVertex
    ) override;

    // Draw a complete mesh with all its submeshes, binding appropriate material for each
    void DrawMesh(MeshID meshId) override;

    // Draw a specific submesh from a mesh
    void DrawSubmesh(MeshID meshId, uint32_t submeshIndex) override;
    bool PrepareTerrain(const std::shared_ptr<const TerrainResource>&) override;
    void DrawTerrain(const TerrainResource&,const TransformComponent&,const TerrainComponent&,
        const std::vector<TerrainDrawChunk>&) override;
    bool TerrainVisible(const glm::vec3&,const glm::vec3&) const override;
    glm::vec3 TerrainCameraPosition() const override;
    size_t TerrainGpuBytes() const override;

    // Dispose completed mesh and texture upload buffers after the GPU fence.
    void CleanupMeshUploadBuffers();
    struct UploadBudget
    {
        size_t maxBytes = 64 * 1024 * 1024;
        double maxMilliseconds = 12.0;
    };
    void SetUploadBudget(const UploadBudget& budget);
    const UploadBudget& GetUploadBudget() const { return mUploadBudget; }

    //void InitDirect3D();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRtvAndDsvDescriptorHeaps();


    ID3D12Resource* CurrentBackBuffer()const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()const;

    void OnResize(int width, int height) override;
	void FlushCommandQueue();

    Dx12ImGuiBindings GetImGuiBindings() const;

private:
    struct SurfacePipelineKey
    {
        SurfaceProgram program;
        SurfaceMode mode = SurfaceMode::Opaque;
        bool twoSided = false;
        DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT identifierFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
        UINT samples = 1;
        UINT sampleQuality = 0;

        auto operator<=>(const SurfacePipelineKey&) const = default;
    };
    struct SurfaceShaderKey
    {
        std::wstring file;
        std::string entry;
        bool pixel = false;
        bool editor = false;
        bool masked = false;

        auto operator<=>(const SurfaceShaderKey&) const = default;
    };
    struct RetiredSurfacePipelines
    {
        UINT64 fence = 0;
        std::vector<ComPtr<ID3D12PipelineState>> pipelines;
        std::vector<ComPtr<ID3D12RootSignature>> rootSignatures;
    };
    std::map<SurfacePipelineKey, ComPtr<ID3D12PipelineState>> mSurfacePipelines;
    std::map<SurfaceShaderKey, ComPtr<ID3DBlob>> mSurfaceShaders;
    std::map<SurfacePipelineKey, std::string> mSurfaceFailures;
    std::deque<RetiredSurfacePipelines> mRetiredSurfacePipelines;
    std::unordered_set<TextureID> mReportedSurfaceTextures;
    SurfaceCacheStats mSurfaceCacheStats;
    std::string mSurfaceDiagnostic;
    SurfacePipelineKey MakeSurfaceKey(const MaterialBase& base, bool editor) const;
    ID3D12PipelineState* FindSurfacePipeline(const SurfacePipelineKey& key);
    ComPtr<ID3D12PipelineState> BuildSurfacePipeline(const SurfacePipelineKey& key);
    ComPtr<ID3DBlob> FindSurfaceShader(const SurfaceShaderKey& key);
    void DrawGpuSubmesh(const MeshGPU& mesh, uint32_t submeshIndex);
    int SurfaceTextureDescriptor(TextureID id, TextureID fallback, bool srgb, DXGI_FORMAT& format, bool* loaded = nullptr);

    struct EditorTarget
    {
        ComPtr<ID3D12Resource> color;
        ComPtr<ID3D12Resource> identifiers;
        ComPtr<ID3D12Resource> depth;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        ComPtr<ID3D12DescriptorHeap> dsvHeap;
        UINT srvIndex = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        UINT64 retireFence = 0;
    };
    struct PickReadback
    {
        ComPtr<ID3D12Resource> buffer;
        std::vector<entt::entity> entities;
        ViewportPickResult result;
        UINT64 fence = 0;
    };
    std::unique_ptr<EditorTarget> mEditorTarget;
    std::deque<std::unique_ptr<EditorTarget>> mRetiredEditorTargets;
    std::vector<UINT> mFreeEditorDescriptors;
    std::deque<PickReadback> mPickReadbacks;
    std::vector<entt::entity> mFrameEntities;
    ViewportPickResult mPendingPick;
    uint32_t mPickX = 0;
    uint32_t mPickY = 0;
    uint32_t mRenderEntityID = 0;
    bool mEditorScene = false;
    bool mSceneVisible = true;
    void CollectEditorTargets();
    struct TerrainGPU
    {
        std::weak_ptr<const TerrainResource> owner;
        std::vector<ComPtr<ID3D12Resource>> textures, vertices, indices, staging;
        std::vector<UINT> indexCounts, vertexBytes;
        UINT descriptors = 0, nextImage = 0;
        UINT64 fence = 0, retireFence = 0;
        size_t bytes = 0;
        bool ready = false;
    };
    std::unordered_map<const TerrainResource*, std::unique_ptr<TerrainGPU>> mTerrainGPU;
    std::vector<UINT> mFreeTerrainDescriptors;
    void BuildTerrainPipeline();
    void CollectTerrainResources();
    size_t mTerrainUploadBytes = 0;
    double mTerrainUploadMs = 0;
    void PumpResourceUploads();
    void EnsureDefaultTextures();
    UploadBudget mUploadBudget;
    bool mTextureUploadTurn = true;
    std::deque<MeshID> mMeshUploads;
    std::unordered_set<MeshID> mQueuedMeshes;
    std::unordered_map<const MeshGeometry*, MeshID> mSharedGeometryUploads;
    HWND      mhMainWnd = nullptr;
    int mClientWidth;
    int mClientHeight;

	float mTotalTime = 0.0f;
	float mDeltaTime = 0.0f;

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

    // Frame-local constant buffer indices.
    UINT mCurrentObjectCBIndex = 0;
    UINT mNextObjectCBIndex = 0;
    UINT mNextMaterialCBIndex = 0;

    // Frame resource management (2 frames in flight for GPU-CPU sync)
    static const int NumFrameResources = 2;
    std::vector<std::unique_ptr<FrameRes>> mFrameResources;
    int mCurrFrameResourceIndex = 0;
    FrameRes* mCurrFrameResource = nullptr;

	PassConstants mMainPassCB;


public:
    // Create SRV from an already loaded ID3D12Resource (returns descriptor index)
    int CreateSRV(ID3D12Resource* resource);

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
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
    std::unordered_map<std::string, ComPtr<ID3D12RootSignature>> mRootSignatures;

    // Input layouts for different vertex formats
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    std::unordered_map<std::string, std::unique_ptr<CustomBuffer>> mBuffers;
    std::unordered_map<std::string, std::vector<DXGI_FORMAT>> mBufferFormats;

    // Update main pass constants (called every frame)
    void UpdateMainPassCB();
    void UpdCB();


    // Upload CPU ImageData from ResourceManager and cache its SRV by TextureID.
    int UploadTexture(TextureID textureId);
    MaterialGPU* GetOrLoadMaterial(MaterialID materialId);

    // Build root signatures and PSOs
    void BuildSurfaceInputLayout();
    void BuildRootSignatures();
    std::vector<D3D12_STATIC_SAMPLER_DESC> GetStaticSamplers();

    // GetMeshGPU uploads primitives on first draw; other CPU-ready meshes use the BeginFrame queue.
    void SetResourceManager(class ResourceManager* resourceManager);
    MeshGPU* UploadMesh(MeshID meshId);
    MeshGPU* GetMeshGPU(MeshID meshId);

    bool      m4xMsaaState = false;    // 4X MSAA enabled
    UINT      m4xMsaaQuality = 0;

    class ResourceManager* mResourceManager = nullptr;
};
