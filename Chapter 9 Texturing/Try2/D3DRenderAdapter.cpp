#include "D3DRenderAdapter.h"
#include "TextureBenchmark.h"

#include <stdexcept>
#include <chrono>
#include <cmath>
#include <assert.h>
#include "ResourceManager.h"
#include <algorithm>

//#define DEBUG

using namespace Microsoft::WRL;

static const int SwapChainBufferCount = 2;

namespace
{
    struct TextureViews
    {
        DXGI_FORMAT resource;
        DXGI_FORMAT linear;
        DXGI_FORMAT srgb;
    };

    TextureViews GetTextureViews(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return {DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return {DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB};
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return {DXGI_FORMAT_B8G8R8X8_TYPELESS, DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB};
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
            return {DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM_SRGB};
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
            return {DXGI_FORMAT_BC2_TYPELESS, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM_SRGB};
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
            return {DXGI_FORMAT_BC3_TYPELESS, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB};
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return {DXGI_FORMAT_BC7_TYPELESS, DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB};
        default:
            return {format, format, DXGI_FORMAT_UNKNOWN};
        }
    }

    void SyncSubmeshMaterials(MeshGPU& gpuMesh, const Mesh& cpuMesh)
    {
        if (gpuMesh.materialVersion == cpuMesh.materialVersion &&
            gpuMesh.submeshes.size() == cpuMesh.submeshes.size())
        {
            return;
        }

        gpuMesh.submeshes.clear();
        gpuMesh.submeshes.reserve(cpuMesh.submeshes.size());
        for (const Mesh::Submesh& cpuSubmesh : cpuMesh.submeshes)
        {
            SubmeshGPU gpuSubmesh;
            gpuSubmesh.indexOffset = cpuSubmesh.indexOffset;
            gpuSubmesh.indexCount = cpuSubmesh.indexCount;
            gpuSubmesh.material = cpuSubmesh.material;
            gpuMesh.submeshes.push_back(gpuSubmesh);
        }

        gpuMesh.materialVersion = cpuMesh.materialVersion;
    }
}

D3DRenderAdapter::~D3DRenderAdapter()
{
    // No texture/mesh/upload heap may disappear while queued commands use it.
    if (mCommandQueue && mFence)
    {
        try { FlushCommandQueue(); }
        catch (...) { OutputDebugStringA("GPU flush failed during renderer shutdown\n"); }
    }
}

void D3DRenderAdapter::SetUploadBudget(const UploadBudget& budget)
{
    if (!budget.maxBytes ||
        !std::isfinite(budget.maxMilliseconds) || budget.maxMilliseconds <= 0)
        throw std::invalid_argument("Upload budget limits must be positive");
    mUploadBudget = budget;
}

void D3DRenderAdapter::EnsureDefaultTextures()
{
    if (!mResourceManager) return;
    if (UploadTexture(mResourceManager->GetPlaceholderTexture()) < 0 ||
        UploadTexture(mResourceManager->GetWhiteTexture()) < 0 ||
        UploadTexture(mResourceManager->GetFlatNormalTexture()) < 0)
        throw std::runtime_error("Could not create default GPU textures");
}

void D3DRenderAdapter::PumpResourceUploads()
{
    if (!mResourceManager) return;
    // This is called only after resetting the frame command list. Uploads and
    // draws use one queue, so copy + transition precede sampling in this frame.
    EnsureDefaultTextures();
    ZoneScopedN("Bounded GPU resource finalization");
    const auto start = std::chrono::steady_clock::now();
    mResourceManager->PumpLoading(mUploadBudget.maxMilliseconds * 0.5);
    size_t uploadedBytes = 0;
    uint32_t textures = 0, meshes = 0;
    bool textureTurn = mTextureUploadTurn;
    mTextureUploadTurn = !mTextureUploadTurn;
    for (;;)
    {
        const TextureID texture = mResourceManager->PeekTextureUpload();
        const bool meshAvailable = !mMeshUploads.empty();
        if (!texture && !meshAvailable) break;
        const bool uploadTexture = texture && (textureTurn || !meshAvailable);
        const MeshID mesh = meshAvailable ? mMeshUploads.front() : 0;
        const size_t bytes = uploadTexture ? mResourceManager->GetTexture(texture).imageData.pixels.size()
            : mResourceManager->GetMesh(mesh).GeometryBytes();
        // At least one resource progresses even if it exceeds the soft budget.
        if (textures + meshes > 0 && (bytes > mUploadBudget.maxBytes - std::min(uploadedBytes, mUploadBudget.maxBytes) ||
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= mUploadBudget.maxMilliseconds))
            break;
        if (uploadTexture)
        {
            if (UploadTexture(texture) < 0)
                mResourceManager->FinishTextureUpload(texture, "GPU texture upload failed (resource creation or SRV capacity)");
            ++textures;
        }
        else
        {
            UploadMesh(mesh);
            mMeshUploads.pop_front();
            mQueuedMeshes.erase(mesh);
            ++meshes;
        }
        uploadedBytes += bytes;
        textureTurn = !uploadTexture;
    }
    // Specialized terrain assets share this frame's soft upload budget.
    mTerrainUploadBytes=uploadedBytes;
    mTerrainUploadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    TracyPlot("Texture uploads this frame", int64_t(textures));
    TracyPlot("Mesh uploads this frame", int64_t(meshes));
    TracyPlot("GPU upload bytes this frame", int64_t(uploadedBytes));
}

// -------------------------------------------------------------
// INIT
// -------------------------------------------------------------

void D3DRenderAdapter::Init(void* windowHandle, uint32_t width, uint32_t height)
{
#if defined(DEBUG) || defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
    }
#endif
    
    mhMainWnd = static_cast<HWND>(windowHandle);
    mClientWidth = width;
    mClientHeight = height;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));

    // Device
    HRESULT hardwareResult = D3D12CreateDevice(
        nullptr,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&md3dDevice));

    if (FAILED(hardwareResult))
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&md3dDevice)));
    }

    ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));

    mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create CBV/SRV/UAV descriptor heap (shader visible). We'll allocate descriptors as needed.
    D3D12_DESCRIPTOR_HEAP_DESC cbvSrvDesc = {};
    cbvSrvDesc.NumDescriptors = 8192; // allow many descriptors for textures and CBVs
    cbvSrvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvSrvDesc, IID_PPV_ARGS(&mCbvSrvUavHeap)));
    mCbvSrvUavDescriptorCount = cbvSrvDesc.NumDescriptors;
    mNextCbvSrvIndex = 1024;

    CreateCommandObjects();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();
    BuildRootSignatures();
    BuildSurfaceInputLayout();
    const MaterialBase defaultSurface;
    for (const bool editor : {false, true})
    {
        const auto key = MakeSurfaceKey(defaultSurface, editor);
        mSurfacePipelines.emplace(key, BuildSurfacePipeline(key));
        ++mSurfaceCacheStats.created;
    }
    BuildTerrainPipeline();

    // Create frame resources (one per frame in flight)
    for (int i = 0; i < NumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameRes>(md3dDevice.Get(), 1, 16384, 16384));
    }
    mCurrFrameResourceIndex = 0;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    OnResize(mClientHeight, mClientWidth);
}

// -------------------------------------------------------------

void D3DRenderAdapter::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

    ThrowIfFailed(md3dDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));

    ThrowIfFailed(md3dDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        mDirectCmdListAlloc.Get(),
        nullptr,
        IID_PPV_ARGS(mCommandList.GetAddressOf())));

    mCommandList->Close();
}

// -------------------------------------------------------------

void D3DRenderAdapter::CreateSwapChain()
{
    mSwapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = mClientWidth;
    sd.BufferDesc.Height = mClientHeight;
    sd.BufferCount = SwapChainBufferCount;
    sd.BufferDesc.Format = mBackBufferFormat;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.OutputWindow = mhMainWnd; 
    sd.SampleDesc.Count = 1;
    sd.Windowed = true;

    ThrowIfFailed(mdxgiFactory->CreateSwapChain(
        mCommandQueue.Get(),
        &sd,
        mSwapChain.GetAddressOf()));
}

// -------------------------------------------------------------

void D3DRenderAdapter::CreateRtvAndDsvDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = SwapChainBufferCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &rtvDesc, IID_PPV_ARGS(&mRtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &dsvDesc, IID_PPV_ARGS(&mDsvHeap)));
}

// -------------------------------------------------------------
// PASS CONSTANTS UPDATE
// -------------------------------------------------------------

void D3DRenderAdapter::UpdateMainPassCB()
{
    if (!mCurrFrameResource) return;

    /*// Build view matrix (camera positioned above and looking at scene)
    DirectX::XMVECTOR pos = DirectX::XMVectorSet(-1.0f, 11.0f, -20.0f, 1.0f);
    DirectX::XMVECTOR target = DirectX::XMVectorSet(cos(mTotalTime*0) - 1.0f, 11.0f, 0.0f + sin(mTotalTime*0) - 20.0f, 1.0f);
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(pos, target, up);
    DirectX::XMMATRIX viewTranspose = DirectX::XMMatrixTranspose(view);
    DirectX::XMStoreFloat4x4(&mMainPassCB.View, viewTranspose);

    // Compute inverse view
    DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
    DirectX::XMMATRIX invViewTranspose = DirectX::XMMatrixTranspose(invView);
    DirectX::XMStoreFloat4x4(&mMainPassCB.InvView, invViewTranspose);

    // Build projection matrix (standard perspective)
    float aspect = mEditorScene ? float(mEditorTarget->width) / mEditorTarget->height : static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight);
    const float fovY = DirectX::XM_PI / 2.0f;  // 45 degrees
    const float nearZ = 1.0f;
    const float farZ = 1500.0f;

    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ);
    DirectX::XMMATRIX projTranspose = DirectX::XMMatrixTranspose(proj);
    DirectX::XMStoreFloat4x4(&mMainPassCB.Proj, projTranspose);

    // Compute inverse projection
    DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
    DirectX::XMMATRIX invProjTranspose = DirectX::XMMatrixTranspose(invProj);
    DirectX::XMStoreFloat4x4(&mMainPassCB.InvProj, invProjTranspose);

    // Compute view-projection matrix
    DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);
    DirectX::XMMATRIX viewProjTranspose = DirectX::XMMatrixTranspose(viewProj);
    DirectX::XMStoreFloat4x4(&mMainPassCB.ViewProj, viewProjTranspose);

    // Compute inverse view-projection matrix
    DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);
    DirectX::XMMATRIX invViewProjTranspose = DirectX::XMMatrixTranspose(invViewProj);
    DirectX::XMStoreFloat4x4(&mMainPassCB.InvViewProj, invViewProjTranspose);

    // Store camera position in world space
    mMainPassCB.EyePosW = DirectX::XMFLOAT3(0.0f, 10.0f, -20.0f);

    // Store render target dimensions
    mMainPassCB.RenderTargetSize = DirectX::XMFLOAT2(static_cast<float>(mClientWidth), static_cast<float>(mClientHeight));
    mMainPassCB.InvRenderTargetSize = DirectX::XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);

    // Store near/far plane distances
    mMainPassCB.NearZ = nearZ;
    mMainPassCB.FarZ = farZ;*/

    // Store timing information (placeholder - can be updated by caller if needed)
    mMainPassCB.TotalTime = mTotalTime;
    mMainPassCB.DeltaTime = mDeltaTime;

    // Copy to GPU constant buffer
    mCurrFrameResource->PassCB->CopyData(0, mMainPassCB);
}

void D3DRenderAdapter::UpdCB()
{
	UpdateMainPassCB();
}

// -------------------------------------------------------------
// FRAME
// -------------------------------------------------------------

void D3DRenderAdapter::BeginFrame()
{
    // Cycle to next frame resource
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % NumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();
    mNextObjectCBIndex = 0;
    mCurrentObjectCBIndex = 0;
    mNextMaterialCBIndex = 0;
    
    ZoneScopedN("BeginFrame");
    // If GPU has not finished processing commands up to this fence, wait
    if (mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        ZoneScopedN("WaitForFence");
		
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    // Reset command allocator for this frame
    ThrowIfFailed(mCurrFrameResource->CmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));
    ID3D12DescriptorHeap* frameHeaps[]={mCbvSrvUavHeap.Get()};
    mCommandList->SetDescriptorHeaps(1,frameHeaps);
    CollectEditorTargets();
    CollectTerrainResources();
    PumpResourceUploads();

    // Update pass constants for this frame
    //UpdateMainPassCB();

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    mCommandList->ResourceBarrier(1, &barrier);

    auto rtv = CurrentBackBufferView();
    auto dsv = DepthStencilView();

    mCommandList->OMSetRenderTargets(1, &rtv, true, &dsv);

    float clearColor[] = { 0.1f, 0.2f, 0.4f, 1.0f };

    mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(dsv,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, nullptr);
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    while (!mRetiredSurfacePipelines.empty() && mRetiredSurfacePipelines.front().fence <= mFence->GetCompletedValue())
    {
        mRetiredSurfacePipelines.pop_front();
    }

}

// -------------------------------------------------------------

Dx12ImGuiBindings D3DRenderAdapter::GetImGuiBindings() const
{
    Dx12ImGuiBindings bindings;
    bindings.Device = md3dDevice.Get();
    bindings.CommandQueue = mCommandQueue.Get();
    bindings.CommandList = mCommandList.Get();
    bindings.SrvHeap = mCbvSrvUavHeap.Get();
    bindings.SrvDescriptorSize = mCbvSrvUavDescriptorSize;
    bindings.RtvFormat = mBackBufferFormat;
    bindings.DsvFormat = mDepthStencilFormat;
    bindings.NumFramesInFlight = NumFrameResources;
    return bindings;
}

void D3DRenderAdapter::EndFrame()
{
    // Transition → Present
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);

    mCommandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));

    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Signal fence for this frame resource
    mCurrentFence++;
    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));
    mCurrFrameResource->Fence = mCurrentFence;
    if (TextureBenchmark::active) {
        if (!TextureBenchmark::uploadFence) {
            size_t loaded = 0;
            for (const auto& [name, texture] : mTextures)
                if (texture->Filename.find(L"StreamingStress") != std::wstring::npos) ++loaded;
            TracyPlot("Benchmark textures ready", int64_t(loaded));
            if (loaded == 1000) TextureBenchmark::uploadFence = mCurrentFence;
        }
        if (TextureBenchmark::uploadFence && mFence->GetCompletedValue() >= TextureBenchmark::uploadFence)
            TextureBenchmark::Ready();
    }


    // Clean up completed mesh uploads
    CleanupMeshUploadBuffers();
}

// -------------------------------------------------------------
// RESIZE (минимальный)
// -------------------------------------------------------------

bool D3DRenderAdapter::ReloadShaders()
{
    auto previousShaders = std::move(mSurfaceShaders);
    auto previousTerrainPipelines = mPSOs;
    auto previousRoots = mRootSignatures;
    decltype(mSurfacePipelines) replacements;

    try
    {
        for (const auto& [key, pipeline] : mSurfacePipelines)
        {
            replacements.emplace(key, BuildSurfacePipeline(key));
            ++mSurfaceCacheStats.created;
        }
        BuildTerrainPipeline();

        RetiredSurfacePipelines retired;
        retired.fence = mCurrentFence + 1;
        for (const auto& [key, pipeline] : mSurfacePipelines)
        {
            retired.pipelines.push_back(pipeline);
        }
        for (const auto& [name, pipeline] : previousTerrainPipelines)
        {
            retired.pipelines.push_back(pipeline);
        }
        for (const auto& [name, signature] : previousRoots)
        {
            retired.rootSignatures.push_back(signature);
        }
        mRetiredSurfacePipelines.push_back(std::move(retired));
        mSurfacePipelines.swap(replacements);
        mSurfaceFailures.clear();
        mSurfaceDiagnostic.clear();
        return true;
    }
    catch (const std::exception& error)
    {
        mSurfaceDiagnostic = error.what();
    }
    catch (const DxException& error)
    {
        mSurfaceDiagnostic = "D3D12 rejected a shader or pipeline (HRESULT " + std::to_string(error.ErrorCode) + ")";
    }

    mSurfaceShaders = std::move(previousShaders);
    mPSOs = std::move(previousTerrainPipelines);
    mRootSignatures = std::move(previousRoots);
    std::cerr << "Shader reload rejected, keeping previous pipelines: " << mSurfaceDiagnostic << '\n';
    return false;
}

void D3DRenderAdapter::OnResize(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    assert(md3dDevice);
    assert(mSwapChain);
    assert(mDirectCmdListAlloc);

    // Flush before changing any resources.
    FlushCommandQueue();

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mClientHeight = height;
	mClientWidth = width;

    // Release the previous resources we will be recreating.
    for (int i = 0; i < SwapChainBufferCount; ++i)
        mSwapChainBuffer[i].Reset();
    mDepthStencilBuffer.Reset();

    // Resize the swap chain.
    ThrowIfFailed(mSwapChain->ResizeBuffers(
        SwapChainBufferCount,
        mClientWidth, mClientHeight,
        mBackBufferFormat,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
	std::cout << "Swap chain resized to " << mClientWidth << "x" << mClientHeight << std::endl;

    mCurrBackBuffer = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SwapChainBufferCount; i++)
    {
        ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
        md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(1, mRtvDescriptorSize);
    }

    // Create the depth/stencil buffer and view.
    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = mClientWidth;
    depthStencilDesc.Height = mClientHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;

    // Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from 
    // the depth buffer.  Therefore, because we need to create two views to the same resource:
    //   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
    //   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
    // we need to create the depth buffer resource with a typeless format.  
    depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

    depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = mDepthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

    // Create descriptor to mip level 0 of entire resource using the format of the resource.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = mDepthStencilFormat;
    dsvDesc.Texture2D.MipSlice = 0;
    md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

    // Transition the resource from its initial state to be used as a depth buffer.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

    // Execute the resize commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until resize is complete.
    FlushCommandQueue();

    // Update the viewport transform to cover the client area.
    mScreenViewport.TopLeftX = 0;
    mScreenViewport.TopLeftY = 0;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f;
    mScreenViewport.MaxDepth = 1.0f;

    mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}

// -------------------------------------------------------------
// HELPERS
// -------------------------------------------------------------

ID3D12Resource* D3DRenderAdapter::CurrentBackBuffer() const
{
    return mSwapChainBuffer[mCurrBackBuffer].Get();
}

int D3DRenderAdapter::CreateSRV(ID3D12Resource* resource)
{
    if (!resource) return -1;
    if (mNextCbvSrvIndex >= mCbvSrvUavDescriptorCount) return -1;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = resource->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = (UINT)resource->GetDesc().MipLevels;

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), mNextCbvSrvIndex, mCbvSrvUavDescriptorSize);
    md3dDevice->CreateShaderResourceView(resource, &srvDesc, handle);

    // keep resource alive
    mOwnedResources.push_back(resource);

    return static_cast<int>(mNextCbvSrvIndex++);
}

// Texture files are decoded by ResourceManager; UploadTexture consumes CPU ImageData.

int D3DRenderAdapter::CreateCBV(const void* data, UINT64 byteSize, ID3D12Resource** outUploadResource)
{
    if (mNextCbvSrvIndex >= mCbvSrvUavDescriptorCount) return -1;

    UINT64 uploadSize = byteSize;
    // Create upload buffer
    ComPtr<ID3D12Resource> uploadBuffer;
    D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(d3dUtils::CalcConstantBufferByteSize((UINT)uploadSize));
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    // Copy data
    void* mapped = nullptr;
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(uploadBuffer->Map(0, &readRange, &mapped));
    memcpy(mapped, data, (size_t)byteSize);
    uploadBuffer->Unmap(0, nullptr);

    // Create CBV descriptor
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = uploadBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtils::CalcConstantBufferByteSize((UINT)byteSize);

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), mNextCbvSrvIndex, mCbvSrvUavDescriptorSize);
    md3dDevice->CreateConstantBufferView(&cbvDesc, handle);

    mOwnedResources.push_back(uploadBuffer);
    if (outUploadResource) *outUploadResource = uploadBuffer.Get();

    return static_cast<int>(mNextCbvSrvIndex++);
}

D3D12_GPU_DESCRIPTOR_HANDLE D3DRenderAdapter::GetGPUDescriptorHandle(UINT index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = mCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (size_t)index * mCbvSrvUavDescriptorSize;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DRenderAdapter::CurrentBackBufferView() const
{
    //return mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE out = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        mCurrBackBuffer,
        mRtvDescriptorSize);

    return out;

}

D3D12_CPU_DESCRIPTOR_HANDLE D3DRenderAdapter::DepthStencilView() const
{
    return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

// -------------------------------------------------------------
// SYNC
// -------------------------------------------------------------

void D3DRenderAdapter::FlushCommandQueue()
{
    mCurrentFence++;

    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

    if (mFence->GetCompletedValue() < mCurrentFence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

void D3DRenderAdapter::SetTransform(const TransformComponent& world) {
    if (!mCurrFrameResource) return;

    mCurrentObjectCBIndex = mNextObjectCBIndex++;
    if(mCurrentObjectCBIndex>=16384) throw std::runtime_error("Frame ObjectCB capacity (16384) exceeded");

    // Build world matrix from transform component (TRS composition)
    ObjectConstants objConstants;
    DirectX::XMFLOAT4X4 worldMatrix = d3dUtils::TransformComponentToWorldMatrix(world);
    DirectX::XMFLOAT4X4 invWorldMatrix = d3dUtils::InvertMatrix4x4(worldMatrix);

    DirectX::XMMATRIX worldXM = DirectX::XMLoadFloat4x4(&worldMatrix);
    DirectX::XMMATRIX invWorldXM = DirectX::XMLoadFloat4x4(&invWorldMatrix);

    DirectX::XMStoreFloat4x4(&objConstants.World, DirectX::XMMatrixTranspose(worldXM));
    DirectX::XMStoreFloat4x4(&objConstants.InvWorld, DirectX::XMMatrixTranspose(invWorldXM));

    DirectX::XMMATRIX texTransformXM = DirectX::XMMatrixIdentity();
    DirectX::XMStoreFloat4x4(&objConstants.TexTransform, DirectX::XMMatrixTranspose(texTransformXM));

    mCurrFrameResource->ObjectCB->CopyData(mCurrentObjectCBIndex, objConstants);
}

void D3DRenderAdapter::SetCamera(const CameraComponent& camera, const TransformComponent& transform)
{
    // Build view matrix (camera positioned above and looking at scene)
    glm::vec3 camPos = transform.position;
    glm::vec3 camRot = transform.rotation; // (pitch, yaw, roll) в радианах

    using namespace DirectX;

    // позиция
    XMVECTOR pos = XMVectorSet(camPos.x, camPos.y, camPos.z, 1.0f);

    // rotation matrix
    XMMATRIX rot = XMMatrixRotationRollPitchYaw(
        camRot.x, // pitch (X)
        camRot.y, // yaw (Y)
        camRot.z  // roll (Z)
    );

    // базовые направления
    XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
    XMVECTOR upDir = XMVectorSet(0, 1, 0, 0);

    // поворачиваем направления
    forward = XMVector3TransformNormal(forward, rot);
    upDir = XMVector3TransformNormal(upDir, rot);

    // target = pos + forward
    XMVECTOR target = XMVectorAdd(pos, forward);

    // view
    XMMATRIX view = XMMatrixLookAtLH(pos, target, upDir);

    // transpose если нужно в шейдер
    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));

    // inverse
    XMMATRIX invView = XMMatrixInverse(nullptr, view);
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));

    // Build projection matrix (standard perspective)
    float aspect = mEditorScene ? float(mEditorTarget->width) / mEditorTarget->height : static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight);
    const float fovY = camera.fov;
    const float nearZ = camera.nearZ;
    const float farZ = camera.farZ;

    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ);
    DirectX::XMMATRIX projTranspose = DirectX::XMMatrixTranspose(proj);
    DirectX::XMStoreFloat4x4(&mMainPassCB.Proj, projTranspose);

    // Compute inverse projection
    DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
    DirectX::XMMATRIX invProjTranspose = DirectX::XMMatrixTranspose(invProj);
    DirectX::XMStoreFloat4x4(&mMainPassCB.InvProj, invProjTranspose);

    // Compute view-projection matrix
    DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);
    DirectX::XMMATRIX viewProjTranspose = DirectX::XMMatrixTranspose(viewProj);
    DirectX::XMStoreFloat4x4(&mMainPassCB.ViewProj, viewProjTranspose);

    // Compute inverse view-projection matrix
    DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);
    DirectX::XMMATRIX invViewProjTranspose = DirectX::XMMatrixTranspose(invViewProj);
    DirectX::XMStoreFloat4x4(&mMainPassCB.InvViewProj, invViewProjTranspose);

    // Store camera position in world space
    mMainPassCB.EyePosW = DirectX::XMFLOAT3(camPos.x, camPos.y, camPos.z);

    // Store render target dimensions
    const float renderWidth = mEditorScene ? float(mEditorTarget->width) : float(mClientWidth);
    const float renderHeight = mEditorScene ? float(mEditorTarget->height) : float(mClientHeight);
    mMainPassCB.RenderTargetSize = DirectX::XMFLOAT2(renderWidth, renderHeight);
    mMainPassCB.InvRenderTargetSize = DirectX::XMFLOAT2(1.0f / renderWidth, 1.0f / renderHeight);

    // Store near/far plane distances
    mMainPassCB.NearZ = nearZ;
    mMainPassCB.FarZ = farZ;
}

void D3DRenderAdapter::SetLights(const SceneLightData& lights)
{
    mMainPassCB.AmbientLight = DirectX::XMFLOAT4(
        lights.ambient.x,
        lights.ambient.y,
        lights.ambient.z,
        lights.ambient.w);

    for (int i = 0; i < MaxLights; ++i)
    {
        mMainPassCB.Lights[i] = Light{};
        mMainPassCB.Lights[i].Strength = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    }

    int lightIndex = 0;
    for (const DirectionalLightData& light : lights.directionalLights)
    {
        if (lightIndex >= RenderDirectionalLightCount || lightIndex >= MaxLights) break;

        mMainPassCB.Lights[lightIndex].Strength = DirectX::XMFLOAT3(
            light.strength.x,
            light.strength.y,
            light.strength.z);
        mMainPassCB.Lights[lightIndex].Direction = DirectX::XMFLOAT3(
            light.direction.x,
            light.direction.y,
            light.direction.z);

        ++lightIndex;
    }

    lightIndex = RenderDirectionalLightCount;
    for (const PointLightData& light : lights.pointLights)
    {
        if (lightIndex >= RenderDirectionalLightCount + RenderPointLightCount || lightIndex >= MaxLights) break;

        mMainPassCB.Lights[lightIndex].Strength = DirectX::XMFLOAT3(
            light.strength.x,
            light.strength.y,
            light.strength.z);
        mMainPassCB.Lights[lightIndex].Position = DirectX::XMFLOAT3(
            light.position.x,
            light.position.y,
            light.position.z);
        mMainPassCB.Lights[lightIndex].FalloffStart = light.falloffStart;
        mMainPassCB.Lights[lightIndex].FalloffEnd = light.falloffEnd;

        ++lightIndex;
    }

    lightIndex = RenderDirectionalLightCount + RenderPointLightCount;
    for (const SpotLightData& light : lights.spotLights)
    {
        if (lightIndex >= RenderDirectionalLightCount + RenderPointLightCount + RenderSpotLightCount || lightIndex >= MaxLights) break;

        mMainPassCB.Lights[lightIndex].Strength = DirectX::XMFLOAT3(
            light.strength.x,
            light.strength.y,
            light.strength.z);
        mMainPassCB.Lights[lightIndex].Position = DirectX::XMFLOAT3(
            light.position.x,
            light.position.y,
            light.position.z);
        mMainPassCB.Lights[lightIndex].Direction = DirectX::XMFLOAT3(
            light.direction.x,
            light.direction.y,
            light.direction.z);
        mMainPassCB.Lights[lightIndex].FalloffStart = light.falloffStart;
        mMainPassCB.Lights[lightIndex].FalloffEnd = light.falloffEnd;
        mMainPassCB.Lights[lightIndex].SpotPower = light.spotPower;

        ++lightIndex;
    }
}

void D3DRenderAdapter::SetMaterial(MaterialID materialId)
{
    if (!mCurrFrameResource || !mResourceManager)
    {
        return;
    }

    const Material& material = mResourceManager->GetMaterial(materialId);
    static const MaterialBase defaultBase;
    const MaterialBase& base = material.base ? *material.base : defaultBase;
    ID3D12PipelineState* pipeline = nullptr;
    if ((!mEditorScene || base.program.editorVariant) &&
        (base.mode != SurfaceMode::Masked || base.program.maskedVariant) &&
        base.mode != SurfaceMode::Translucent)
    {
        pipeline = FindSurfacePipeline(MakeSurfaceKey(base, mEditorScene));
    }
    else
    {
        const auto key = MakeSurfaceKey(base, mEditorScene);
        const auto [failure, added] = mSurfaceFailures.try_emplace(key, "Unsupported surface mode or shader variant");
        if (added)
        {
            mSurfaceDiagnostic = failure->second;
            std::cerr << material.name << ": " << mSurfaceDiagnostic << '\n';
        }
    }
    if (!pipeline)
    {
        MaterialBase fallback = defaultBase;
        fallback.mode = base.mode == SurfaceMode::Masked ? SurfaceMode::Masked : SurfaceMode::Opaque;
        fallback.twoSided = base.twoSided;
        pipeline = FindSurfacePipeline(MakeSurfaceKey(fallback, mEditorScene));
        if (!pipeline)
        {
            pipeline = mSurfacePipelines.at(MakeSurfaceKey(defaultBase, mEditorScene)).Get();
        }
    }

    const MaterialGPU& gpu = *GetOrLoadMaterial(materialId);
    const auto& parameters = material.parameters;
    const auto finiteOr = [](float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    };
    const auto unit = [&](float value, float fallback)
    {
        return std::clamp(finiteOr(value, fallback), 0.0f, 1.0f);
    };
    MaterialConstants constants{};
    constants.baseColor = {unit(parameters.baseColor.r, 1), unit(parameters.baseColor.g, 1),
        unit(parameters.baseColor.b, 1), unit(parameters.baseColor.a, 1)};
    constants.emissiveColor = {unit(parameters.emissiveColor.r, 0),
        unit(parameters.emissiveColor.g, 0), unit(parameters.emissiveColor.b, 0)};
    constants.emissiveIntensity = std::max(0.0f, finiteOr(parameters.emissiveIntensity, 1));
    constants.uvScale = {finiteOr(parameters.uvScale.x, 1), finiteOr(parameters.uvScale.y, 1)};
    constants.uvOffset = {finiteOr(parameters.uvOffset.x, 0), finiteOr(parameters.uvOffset.y, 0)};
    constants.roughness = unit(parameters.roughness, 0.5f);
    const float defaultMetallic = gpu.hasORM ? 1.0f : 0.0f;
    constants.metallic = unit(parameters.metallic.value_or(defaultMetallic), defaultMetallic);
    constants.normalStrength = std::clamp(finiteOr(parameters.normalStrength, 1), 0.0f, 8.0f);
    constants.aoStrength = unit(parameters.aoStrength, 1);
    constants.alphaCutoff = unit(parameters.alphaCutoff, 0.5f);
    constants.textureFlags = gpu.textureFlags;

    const UINT materialIndex = mNextMaterialCBIndex++;
    if (materialIndex >= 16384)
    {
        throw std::runtime_error("Frame MaterialCB capacity (16384) exceeded");
    }
    auto& frame = *mCurrFrameResource;
    frame.MaterialCB->CopyData(materialIndex, constants);

    ID3D12DescriptorHeap* heaps[] = {mCbvSrvUavHeap.Get()};
    mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);
    mCommandList->SetPipelineState(pipeline);
    mCommandList->SetGraphicsRootSignature(mRootSignatures.at("standard").Get());
    mCommandList->SetGraphicsRootDescriptorTable(0, GetGPUDescriptorHandle(gpu.DiffuseSrvHeapIndex));
    mCommandList->SetGraphicsRootDescriptorTable(1, GetGPUDescriptorHandle(gpu.NormalSrvHeapIndex));
    mCommandList->SetGraphicsRootDescriptorTable(6, GetGPUDescriptorHandle(gpu.ormSrvIndex));
    mCommandList->SetGraphicsRootDescriptorTable(7, GetGPUDescriptorHandle(gpu.emissiveSrvIndex));
    mCommandList->SetGraphicsRootConstantBufferView(2, frame.ObjectCB->Resource()->GetGPUVirtualAddress() +
        UINT64(mCurrentObjectCBIndex) * frame.ObjectCB->ElementByteSize());
    mCommandList->SetGraphicsRootConstantBufferView(3, frame.PassCB->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootConstantBufferView(4, frame.MaterialCB->Resource()->GetGPUVirtualAddress() +
        UINT64(materialIndex) * frame.MaterialCB->ElementByteSize());
    mCommandList->SetGraphicsRoot32BitConstant(5, mRenderEntityID, 0);
}

void D3DRenderAdapter::SetTimeData(float TotalTime, float DeltaTime)
{
	mTotalTime = TotalTime;
	mDeltaTime = DeltaTime;
}

void D3DRenderAdapter::DrawIndexed(
    uint32_t indexCount,
    uint32_t startIndex,
    int32_t baseVertex)  {

    // Bind CBV/SRV heap
    ID3D12DescriptorHeap* heaps[] = { mCbvSrvUavHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

    //mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // For now we do not perform actual drawing because higher-level code sets PSO and root signature.
    // This function is a placeholder that would bind per-draw SRVs/CBVs (material, transform) when available.

}

// -------------------------------------------------------------
// ROOT SIGNATURES AND PSOs
// -------------------------------------------------------------

void D3DRenderAdapter::BuildSurfaceInputLayout()
{
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}



void D3DRenderAdapter::BuildRootSignatures()
{
    // Root signature for forward lighting pass (opaque objects)
    // Each draw call renders one fully lit object to the back buffer
    {
        CD3DX12_DESCRIPTOR_RANGE albedoRange;
        albedoRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 - albedo texture

        CD3DX12_DESCRIPTOR_RANGE normalRange;
        normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1 - normal texture

        CD3DX12_DESCRIPTOR_RANGE ormRange;
        ormRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
        CD3DX12_DESCRIPTOR_RANGE emissiveRange;
        emissiveRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);

        CD3DX12_ROOT_PARAMETER rootParams[8];
        rootParams[0].InitAsDescriptorTable(1, &albedoRange, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[1].InitAsDescriptorTable(1, &normalRange, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL); // b0 - object/world cbuffer
        rootParams[3].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL); // b1 - pass cbuffer (camera, lights)
        rootParams[4].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_ALL); // b2 - material cbuffer

        rootParams[5].InitAsConstants(1, 3, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[6].InitAsDescriptorTable(1, &ormRange, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[7].InitAsDescriptorTable(1, &emissiveRange, D3D12_SHADER_VISIBILITY_PIXEL);

        auto staticSamplers = GetStaticSamplers();

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
            _countof(rootParams), rootParams,
            (UINT)staticSamplers.size(), staticSamplers.data(),
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serializedRootSig = nullptr;
        ComPtr<ID3DBlob> errorBlob = nullptr;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
            serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));

        if (errorBlob)
        {
            ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }

        ThrowIfFailed(md3dDevice->CreateRootSignature(
            0,
            serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(),
            IID_PPV_ARGS(&mRootSignatures["standard"])));

    }
}

// -------------------------------------------------------------
// MESH UPLOAD
// -------------------------------------------------------------

void D3DRenderAdapter::SetResourceManager(ResourceManager* resourceManager)
{
    mResourceManager = resourceManager;
    mMeshUploads.clear();
    mQueuedMeshes.clear();
    mSharedGeometryUploads.clear();
}

MeshGPU* D3DRenderAdapter::UploadMesh(MeshID meshId)
{
    // Check if already uploaded
    auto it = mGeometries.find(meshId);
    if (it != mGeometries.end())
    {
        return it->second.get();
    }

    if (!mResourceManager)
    {
        throw std::runtime_error("ResourceManager not set on D3DRenderAdapter");
    }

    // Get CPU mesh data from ResourceManager
    Mesh& cpuMesh = mResourceManager->GetMesh(meshId);

    if (cpuMesh.state == ResourceState::Loading || cpuMesh.state == ResourceState::Failed) return nullptr;
    const auto& vertices = cpuMesh.GetVertices();
    const auto& indices = cpuMesh.GetIndices();
    if (vertices.empty() || indices.empty())
    {
        throw std::runtime_error("Mesh has no vertices or indices");
    }

    auto meshGPU = std::make_unique<MeshGPU>();

    // Materials belong to each MeshID; immutable primitive buffers belong to the geometry.
    MeshGPU* shared = nullptr;
    if (cpuMesh.sharedGeometry)
        if (auto cached = mSharedGeometryUploads.find(cpuMesh.sharedGeometry.get()); cached != mSharedGeometryUploads.end())
            shared = mGeometries.at(cached->second).get();
    if (shared)
    {
        meshGPU->vertexBuffer = shared->vertexBuffer;
        meshGPU->indexBuffer = shared->indexBuffer;
        meshGPU->vbView = shared->vbView;
        meshGPU->ibView = shared->ibView;
        meshGPU->indexCount = shared->indexCount;
    }
    else
    {
        const UINT64 vbByteSize = UINT64(vertices.size()) * sizeof(Vertex);
        const UINT64 ibByteSize = UINT64(indices.size()) * sizeof(uint32_t);
        meshGPU->vertexBuffer = d3dUtils::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
            vertices.data(), vbByteSize, meshGPU->vertexUploadBuffer);
        meshGPU->indexBuffer = d3dUtils::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
            indices.data(), ibByteSize, meshGPU->indexUploadBuffer);
        meshGPU->vbView = { meshGPU->vertexBuffer->GetGPUVirtualAddress(), UINT(vbByteSize), sizeof(Vertex) };
        meshGPU->ibView = { meshGPU->indexBuffer->GetGPUVirtualAddress(), UINT(ibByteSize), DXGI_FORMAT_R32_UINT };
        meshGPU->indexCount = UINT(indices.size());
        // Only the first mesh owns staging buffers, retained until this fence completes.
        meshGPU->uploadCompleteFence = mCurrentFence + 1;
    }

    // Copy submesh metadata from CPU to GPU
    meshGPU->submeshes.reserve(cpuMesh.submeshes.size());
    for (const auto& cpuSubmesh : cpuMesh.submeshes)
    {
        SubmeshGPU gpuSubmesh;
        gpuSubmesh.indexOffset = cpuSubmesh.indexOffset;
        gpuSubmesh.indexCount = cpuSubmesh.indexCount;
        gpuSubmesh.material = cpuSubmesh.material;
        meshGPU->submeshes.push_back(gpuSubmesh);
    }
    meshGPU->materialVersion = cpuMesh.materialVersion;

    MeshGPU* result = meshGPU.get();
    mGeometries[meshId] = std::move(meshGPU);
    if (cpuMesh.sharedGeometry && !shared)
        mSharedGeometryUploads.emplace(cpuMesh.sharedGeometry.get(), meshId);
    mResourceManager->FinishMeshUpload(meshId);

    return result;
}

MeshGPU* D3DRenderAdapter::GetMeshGPU(MeshID meshId)
{
    // Return if already uploaded
    auto it = mGeometries.find(meshId);
    if (it != mGeometries.end())
    {
        if (mResourceManager)
        {
            Mesh& cpuMesh = mResourceManager->GetMesh(meshId);
            SyncSubmeshMaterials(*it->second, cpuMesh);
        }

        return it->second.get();
    }

    if (!mResourceManager) return nullptr;
    const auto& mesh = mResourceManager->GetMesh(meshId);
    if (mesh.state == ResourceState::Loading || mesh.state == ResourceState::Failed) return nullptr;
    // Record the copies and barriers before this draw so a primitive needs no extra frame.
    if (mesh.isPrimitive) return UploadMesh(meshId);
    if (mQueuedMeshes.insert(meshId).second) mMeshUploads.push_back(meshId);
    return nullptr; // Finalization occurs in BeginFrame, within the upload budget.
}

void D3DRenderAdapter::DrawMesh(MeshID meshId)
{
    const MeshGPU* mesh = GetMeshGPU(meshId);
    if (!mesh)
    {
        return;
    }
    for (uint32_t submeshIndex = 0; submeshIndex < mesh->submeshes.size(); ++submeshIndex)
    {
        DrawGpuSubmesh(*mesh, submeshIndex);
    }
}

void D3DRenderAdapter::DrawSubmesh(MeshID meshId, uint32_t submeshIndex)
{
    const MeshGPU* mesh = GetMeshGPU(meshId);
    if (mesh && submeshIndex < mesh->submeshes.size())
    {
        DrawGpuSubmesh(*mesh, submeshIndex);
    }
}

void D3DRenderAdapter::DrawGpuSubmesh(const MeshGPU& mesh, uint32_t submeshIndex)
{
    const auto& submesh = mesh.submeshes[submeshIndex];
    SetMaterial(submesh.material);
    mCommandList->IASetVertexBuffers(0, 1, &mesh.vbView);
    mCommandList->IASetIndexBuffer(&mesh.ibView);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->DrawIndexedInstanced(submesh.indexCount, 1, submesh.indexOffset, 0, 0);
}

int D3DRenderAdapter::UploadTexture(TextureID textureId)
{
    const std::string key = std::to_string(textureId);
    auto cached = mTextures.find(key);
    if (cached != mTextures.end()) return cached->second->SrvHeapIndex;
    if (!mResourceManager || mNextCbvSrvIndex >= mCbvSrvUavDescriptorCount) return -1;

    const Texture& cpuTexture = mResourceManager->GetTexture(textureId);
    const ImageData& image = cpuTexture.imageData;
    if (cpuTexture.state == ResourceState::Loading || cpuTexture.state == ResourceState::Failed ||
        image.pixels.empty() || image.subresources.empty()) return -1;
    ZoneScopedN("Texture GPU upload");

    const TextureViews views = GetTextureViews(image.format);
    const UINT descriptorCount = views.srgb == DXGI_FORMAT_UNKNOWN ? 1 : 2;
    if (descriptorCount > mCbvSrvUavDescriptorCount - mNextCbvSrvIndex)
    {
        return -1;
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = image.dimension == ImageDimension::Texture1D ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
        : image.dimension == ImageDimension::Texture3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
        : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = image.width;
    desc.Height = image.height;
    desc.DepthOrArraySize = static_cast<UINT16>(
        image.dimension == ImageDimension::Texture3D ? image.depth : image.arraySize);
    desc.MipLevels = static_cast<UINT16>(image.mipLevels);
    desc.Format = views.resource;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    auto textureGPU = std::make_unique<TextureGPU>();
    textureGPU->Filename = cpuTexture.filename;
    textureGPU->Name = cpuTexture.name;
    textureGPU->viewFormat = image.format;
    const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = md3dDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&textureGPU->Resource));
    if (FAILED(hr)) return -1;

    const UINT subresourceCount = static_cast<UINT>(image.subresources.size());
    const UINT64 uploadSize = GetRequiredIntermediateSize(textureGPU->Resource.Get(), 0, subresourceCount);
    const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    const auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    hr = md3dDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureGPU->UploadHeap));
    if (FAILED(hr)) return -1;

    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    subresources.reserve(subresourceCount);
    for (const ImageSubresource& source : image.subresources)
    {
        subresources.push_back({ image.pixels.data() + source.offset,
            static_cast<LONG_PTR>(source.rowPitch), static_cast<LONG_PTR>(source.slicePitch) });
    }
    if (!UpdateSubresources(mCommandList.Get(), textureGPU->Resource.Get(),
        textureGPU->UploadHeap.Get(), 0, 0, subresourceCount, subresources.data())) return -1;
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(textureGPU->Resource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCommandList->ResourceBarrier(1, &barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = image.format;
    if (image.dimension == ImageDimension::Texture3D)
    {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = image.mipLevels;
    }
    else if (image.dimension == ImageDimension::Texture1D)
    {
        if (image.arraySize > 1)
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            srvDesc.Texture1DArray.MipLevels = image.mipLevels;
            srvDesc.Texture1DArray.ArraySize = image.arraySize;
        }
        else
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            srvDesc.Texture1D.MipLevels = image.mipLevels;
        }
    }
    else if (image.isCubeMap)
    {
        if (image.arraySize > 6)
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            srvDesc.TextureCubeArray.MipLevels = image.mipLevels;
            srvDesc.TextureCubeArray.NumCubes = image.arraySize / 6;
        }
        else
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MipLevels = image.mipLevels;
        }
    }
    else if (image.arraySize > 1)
    {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MipLevels = image.mipLevels;
        srvDesc.Texture2DArray.ArraySize = image.arraySize;
    }
    else
    {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = image.mipLevels;
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
        mNextCbvSrvIndex, mCbvSrvUavDescriptorSize);
    md3dDevice->CreateShaderResourceView(textureGPU->Resource.Get(), &srvDesc, handle);
    const int srvIndex = static_cast<int>(mNextCbvSrvIndex++);
    textureGPU->SrvHeapIndex = srvIndex;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{views.linear};
    textureGPU->surfaceTexture = srvDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D &&
        SUCCEEDED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
        (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0;
    if (views.srgb != DXGI_FORMAT_UNKNOWN)
    {
        srvDesc.Format = image.format == views.srgb ? views.linear : views.srgb;
        handle.Offset(1, mCbvSrvUavDescriptorSize);
        md3dDevice->CreateShaderResourceView(textureGPU->Resource.Get(), &srvDesc, handle);
        textureGPU->alternateSrvIndex = static_cast<int>(mNextCbvSrvIndex++);
    }
    textureGPU->uploadCompleteFence = mCurrentFence + 1;
    mTextures[key] = std::move(textureGPU);
    mResourceManager->FinishTextureUpload(textureId);
    return srvIndex;
}

void D3DRenderAdapter::CleanupMeshUploadBuffers()
{
    // Dispose upload buffers once GPU has finished processing them
    for (auto& [meshId, meshGPU] : mGeometries)
    {
        // Only clean up if upload is marked complete and GPU has reached that fence
        if (meshGPU->uploadCompleteFence > 0 && 
            mFence->GetCompletedValue() >= meshGPU->uploadCompleteFence)
        {
            meshGPU->vertexUploadBuffer.Reset();
            meshGPU->indexUploadBuffer.Reset();
            meshGPU->uploadCompleteFence = 0;  // Mark as cleaned up
        }
    }
    for (auto& [key, texture] : mTextures)
    {
        if (texture->uploadCompleteFence && mFence->GetCompletedValue() >= texture->uploadCompleteFence)
        {
            texture->UploadHeap.Reset();
            texture->uploadCompleteFence = 0;
        }
    }
}

std::vector<D3D12_STATIC_SAMPLER_DESC> D3DRenderAdapter::GetStaticSamplers()
{
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;

    // Point wrap (s0)
    D3D12_STATIC_SAMPLER_DESC pointWrap = {};
    pointWrap.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    pointWrap.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    pointWrap.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    pointWrap.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    pointWrap.MipLODBias = 0.0f;
    pointWrap.MaxAnisotropy = 1;
    pointWrap.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pointWrap.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    pointWrap.MinLOD = 0.0f;
    pointWrap.MaxLOD = D3D12_FLOAT32_MAX;
    pointWrap.ShaderRegister = 0;
    pointWrap.RegisterSpace = 0;
    pointWrap.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers.push_back(pointWrap);

    // Point clamp (s1)
    D3D12_STATIC_SAMPLER_DESC pointClamp = {};
    pointClamp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    pointClamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointClamp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointClamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointClamp.MipLODBias = 0.0f;
    pointClamp.MaxAnisotropy = 1;
    pointClamp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pointClamp.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    pointClamp.MinLOD = 0.0f;
    pointClamp.MaxLOD = D3D12_FLOAT32_MAX;
    pointClamp.ShaderRegister = 1;
    pointClamp.RegisterSpace = 0;
    pointClamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers.push_back(pointClamp);

    // Linear wrap (s2)
    D3D12_STATIC_SAMPLER_DESC linearWrap = {};
    linearWrap.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linearWrap.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    linearWrap.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    linearWrap.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    linearWrap.MipLODBias = 0.0f;
    linearWrap.MaxAnisotropy = 1;
    linearWrap.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    linearWrap.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    linearWrap.MinLOD = 0.0f;
    linearWrap.MaxLOD = D3D12_FLOAT32_MAX;
    linearWrap.ShaderRegister = 2;
    linearWrap.RegisterSpace = 0;
    linearWrap.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers.push_back(linearWrap);

    // Linear clamp (s3)
    D3D12_STATIC_SAMPLER_DESC linearClamp = {};
    linearClamp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linearClamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linearClamp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linearClamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linearClamp.MipLODBias = 0.0f;
    linearClamp.MaxAnisotropy = 1;
    linearClamp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    linearClamp.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    linearClamp.MinLOD = 0.0f;
    linearClamp.MaxLOD = D3D12_FLOAT32_MAX;
    linearClamp.ShaderRegister = 3;
    linearClamp.RegisterSpace = 0;
    linearClamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers.push_back(linearClamp);

    // Anisotropic wrap (s4)
    D3D12_STATIC_SAMPLER_DESC anisotropicWrap = {};
    anisotropicWrap.Filter = D3D12_FILTER_ANISOTROPIC;
    anisotropicWrap.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    anisotropicWrap.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    anisotropicWrap.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    anisotropicWrap.MipLODBias = 0.0f;
    anisotropicWrap.MaxAnisotropy = 8;
    anisotropicWrap.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    anisotropicWrap.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    anisotropicWrap.MinLOD = 0.0f;
    anisotropicWrap.MaxLOD = D3D12_FLOAT32_MAX;
    anisotropicWrap.ShaderRegister = 4;
    anisotropicWrap.RegisterSpace = 0;
    anisotropicWrap.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers.push_back(anisotropicWrap);

    // Anisotropic clamp (s5)
    D3D12_STATIC_SAMPLER_DESC anisotropicClamp = {};
    anisotropicClamp.Filter = D3D12_FILTER_ANISOTROPIC;
    anisotropicClamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    anisotropicClamp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    anisotropicClamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    anisotropicClamp.MipLODBias = 0.0f;
    anisotropicClamp.MaxAnisotropy = 8;
    anisotropicClamp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    anisotropicClamp.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    anisotropicClamp.MinLOD = 0.0f;
    anisotropicClamp.MaxLOD = D3D12_FLOAT32_MAX;
    anisotropicClamp.ShaderRegister = 5;
    anisotropicClamp.RegisterSpace = 0;
    anisotropicClamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers.push_back(anisotropicClamp);

    return samplers;
}

// End of file
