#include "D3DRenderAdapter.h"

#include <stdexcept>
#include <vector>
#include <assert.h>
#include "ResourceManager.h"
#include "DDSTextureLoader.h"

//#define DEBUG

using namespace Microsoft::WRL;

static const int SwapChainBufferCount = 2;

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
    cbvSrvDesc.NumDescriptors = 1024; // allow many descriptors for textures and CBVs
    cbvSrvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvSrvDesc, IID_PPV_ARGS(&mCbvSrvUavHeap)));
    mCbvSrvUavDescriptorCount = cbvSrvDesc.NumDescriptors;
    mNextCbvSrvIndex = 0;

    CreateCommandObjects();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();
    BuildRootSignatures();
    BuildShadersAndInputLayout();
    BuildPSOs();
    BuildColliderBoxGeometry();

    // Create frame resources (one per frame in flight)
    for (int i = 0; i < NumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameRes>(md3dDevice.Get(), 1, 1024, 512));
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
    float aspect = static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight);
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

    // If GPU has not finished processing commands up to this fence, wait
    if (mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    // Reset command allocator for this frame
    ThrowIfFailed(mCurrFrameResource->CmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));

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

    mCommandList->SetPipelineState(mPSOs["opaque"].Get());
    mCommandList->SetGraphicsRootSignature(mRootSignatures["standard"].Get());

    mCommandList->SetGraphicsRootConstantBufferView(3, mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());

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

    ThrowIfFailed(mSwapChain->Present(1, 0));

    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Signal fence for this frame resource
    mCurrentFence++;
    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));
    mCurrFrameResource->Fence = mCurrentFence;

    // Clean up completed mesh uploads
    CleanupMeshUploadBuffers();
}

// -------------------------------------------------------------
// RESIZE (минимальный)
// -------------------------------------------------------------

bool D3DRenderAdapter::ReloadShaders()
{
    auto previousShaders = mShaders;
    auto previousPSOs = mPSOs;
    auto previousInputLayout = mInputLayout;

    try
    {
        BuildShadersAndInputLayout();
        BuildPSOs();

        if (mCommandList && mPSOs["opaque"] && mRootSignatures["standard"])
        {
            mCommandList->SetPipelineState(mPSOs["opaque"].Get());
            mCommandList->SetGraphicsRootSignature(mRootSignatures["standard"].Get());
        }

        OutputDebugStringA("Shaders reloaded successfully.\n");
        return true;
    }
    catch (const DxException& e)
    {
        mShaders = previousShaders;
        mPSOs = previousPSOs;
        mInputLayout = previousInputLayout;

        OutputDebugStringA("Shader reload failed. Keeping previous shaders.\n");
        OutputDebugStringW(e.ToString().c_str());
        OutputDebugStringW(L"\n");
    }
    catch (const std::exception& e)
    {
        mShaders = previousShaders;
        mPSOs = previousPSOs;
        mInputLayout = previousInputLayout;

        OutputDebugStringA("Shader reload failed. Keeping previous shaders: ");
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
    }
    catch (...)
    {
        mShaders = previousShaders;
        mPSOs = previousPSOs;
        mInputLayout = previousInputLayout;

        OutputDebugStringA("Shader reload failed with unknown error. Keeping previous shaders.\n");
    }

    if (mCommandList && mPSOs["opaque"] && mRootSignatures["standard"])
    {
        mCommandList->SetPipelineState(mPSOs["opaque"].Get());
        mCommandList->SetGraphicsRootSignature(mRootSignatures["standard"].Get());
    }

    return false;
}

void D3DRenderAdapter::OnResize(int width, int height)
{
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

// CreateSRVFromFile removed - use ResourceManager/DDSTextureLoader in a module that links DirectX helper.

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
    mCurrentTransform = world;
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
    float aspect = static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight);
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
    mMainPassCB.RenderTargetSize = DirectX::XMFLOAT2(static_cast<float>(mClientWidth), static_cast<float>(mClientHeight));
    mMainPassCB.InvRenderTargetSize = DirectX::XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);

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

void D3DRenderAdapter::SetMaterial(MaterialID material) {
    if (!mCurrFrameResource) return;

    MaterialGPU* matGPU = GetOrLoadMaterial(material);
    if (!matGPU) return;

    MaterialConstants matConstants;
    matConstants.DiffuseAlbedo = matGPU->DiffuseAlbedo;
    matConstants.FresnelR0 = matGPU->FresnelR0;
    matConstants.Roughness = matGPU->Roughness;
    matConstants.MatTransform = matGPU->MatTransform;

    const UINT materialCBIndex = mNextMaterialCBIndex++;
    mCurrFrameResource->MaterialCB->CopyData(materialCBIndex, matConstants);

    D3D12_GPU_VIRTUAL_ADDRESS matCBAddress =
        mCurrFrameResource->MaterialCB->Resource()->GetGPUVirtualAddress() +
        static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(materialCBIndex) *
        mCurrFrameResource->MaterialCB->ElementByteSize();
    mCommandList->SetGraphicsRootConstantBufferView(4, matCBAddress);

    mCurrentMaterial = material;
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

void D3DRenderAdapter::BuildShadersAndInputLayout()
{
    mShaders["standardVS"] = d3dUtils::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    if (!mShaders["standardVS"]) throw std::runtime_error("Failed to compile vertex shader");

    mShaders["standardPS"] = d3dUtils::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
    if (!mShaders["standardPS"]) throw std::runtime_error("Failed to compile pixel shader");

    mShaders["colliderDebugVS"] = d3dUtils::CompileShader(L"Shaders\\BoundingBoxDebug.hlsl", nullptr, "VSMain", "vs_5_1");
    if (!mShaders["colliderDebugVS"]) throw std::runtime_error("Failed to compile collider debug vertex shader");

    mShaders["colliderDebugPS"] = d3dUtils::CompileShader(L"Shaders\\BoundingBoxDebug.hlsl", nullptr, "PSMain", "ps_5_1");
    if (!mShaders["colliderDebugPS"]) throw std::runtime_error("Failed to compile collider debug pixel shader");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    mColliderDebugInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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

        CD3DX12_ROOT_PARAMETER rootParams[5];
        rootParams[0].InitAsDescriptorTable(1, &albedoRange, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[1].InitAsDescriptorTable(1, &normalRange, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL); // b0 - object/world cbuffer
        rootParams[3].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL); // b1 - pass cbuffer (camera, lights)
        rootParams[4].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_ALL); // b2 - material cbuffer

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

    // Collider AABB wireframe: pass CB (b1) + object CB (b0)
    {
        CD3DX12_ROOT_PARAMETER rootParams[2];
        rootParams[0].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
        rootParams[1].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
            _countof(rootParams), rootParams,
            0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serializedRootSig = nullptr;
        ComPtr<ID3DBlob> errorBlob = nullptr;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
            serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));

        if (errorBlob)
            ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());

        ThrowIfFailed(md3dDevice->CreateRootSignature(
            0,
            serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(),
            IID_PPV_ARGS(&mRootSignatures["colliderDebug"])));
    }
}

void D3DRenderAdapter::BuildPSOs()
{
    // PSO for forward lighting: single render target, one draw call per lit object
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

    if (!mShaders["standardVS"] || !mShaders["standardPS"])
        throw std::runtime_error("Shaders not compiled before BuildPSOs");

    opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    opaquePsoDesc.pRootSignature = mRootSignatures["standard"].Get();
    opaquePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS = 
    {
        reinterpret_cast<BYTE*>(mShaders["standardPS"]->GetBufferPointer()),
        mShaders["standardPS"]->GetBufferSize()
    };
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // Single render target: directly render to back buffer with albedo + normal-based lighting
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    if (!mShaders["colliderDebugVS"] || !mShaders["colliderDebugPS"])
        throw std::runtime_error("Collider debug shaders not compiled before BuildPSOs");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = opaquePsoDesc;
    debugPsoDesc.InputLayout = { mColliderDebugInputLayout.data(), (UINT)mColliderDebugInputLayout.size() };
    debugPsoDesc.pRootSignature = mRootSignatures["colliderDebug"].Get();
    debugPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["colliderDebugVS"]->GetBufferPointer()),
        mShaders["colliderDebugVS"]->GetBufferSize()
    };
    debugPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["colliderDebugPS"]->GetBufferPointer()),
        mShaders["colliderDebugPS"]->GetBufferSize()
    };
    debugPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    debugPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    debugPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    debugPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["colliderDebug"])));
}

void D3DRenderAdapter::BuildColliderBoxGeometry()
{
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    struct DebugBoxVertex
    {
        DirectX::XMFLOAT3 Pos;
    };

    const DebugBoxVertex unitCorners[8] =
    {
        { {-0.5f, -0.5f, -0.5f} },
        { { 0.5f, -0.5f, -0.5f} },
        { { 0.5f,  0.5f, -0.5f} },
        { {-0.5f,  0.5f, -0.5f} },
        { {-0.5f, -0.5f,  0.5f} },
        { { 0.5f, -0.5f,  0.5f} },
        { { 0.5f,  0.5f,  0.5f} },
        { {-0.5f,  0.5f,  0.5f} },
    };

    const uint16_t edgePairs[] =
    {
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7
    };

    std::vector<DebugBoxVertex> vertices;
    vertices.reserve(sizeof(edgePairs) / sizeof(edgePairs[0]));
    for (uint16_t index : edgePairs)
        vertices.push_back(unitCorners[index]);

    mColliderBoxVertexCount = static_cast<UINT>(vertices.size());
    const UINT64 byteSize = static_cast<UINT64>(vertices.size() * sizeof(DebugBoxVertex));

    ComPtr<ID3D12Resource> uploadBuffer;
    mColliderBoxVertexBuffer = d3dUtils::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        vertices.data(),
        byteSize,
        uploadBuffer);

    mOwnedResources.push_back(mColliderBoxVertexBuffer);

    mColliderBoxVBView.BufferLocation = mColliderBoxVertexBuffer->GetGPUVirtualAddress();
    mColliderBoxVBView.StrideInBytes = sizeof(DebugBoxVertex);
    mColliderBoxVBView.SizeInBytes = static_cast<UINT>(byteSize);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* lists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, lists);
    FlushCommandQueue();
direc    // Leave the command list closed. OnResize / BeginFrame will reset the allocator and list.
}

void D3DRenderAdapter::DrawColliderBoundingBoxes(
    entt::registry& registry,
    ColliderBoundsDebugMode mode,
    entt::entity selectedEntity)
{
    if (mode == ColliderBoundsDebugMode::None || !mCurrFrameResource || !mColliderBoxVertexBuffer)
        return;

    auto psoIt = mPSOs.find("colliderDebug");
    auto rootIt = mRootSignatures.find("colliderDebug");
    if (psoIt == mPSOs.end() || rootIt == mRootSignatures.end())
        return;

    mCommandList->SetPipelineState(psoIt->second.Get());
    mCommandList->SetGraphicsRootSignature(rootIt->second.Get());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    mCommandList->IASetVertexBuffers(0, 1, &mColliderBoxVBView);

    mCommandList->SetGraphicsRootConstantBufferView(
        0,
        mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());

    auto drawEntity = [&](entt::entity entity)
    {
        if (!registry.valid(entity))
            return;
        if (!registry.all_of<TransformComponent, ColliderComponent>(entity))
            return;

        const auto& transform = registry.get<TransformComponent>(entity);
        const auto& collider = registry.get<ColliderComponent>(entity);
        const AABB bounds = MakeScaledAABB(transform.position, transform.scale, collider);

        TransformComponent boxTransform;
        boxTransform.position = bounds.center;
        boxTransform.rotation = glm::vec3(0.0f);
        boxTransform.scale = bounds.halfExtents * 2.0f;
        SetTransform(boxTransform);

        const D3D12_GPU_VIRTUAL_ADDRESS objectCBAddress =
            mCurrFrameResource->ObjectCB->Resource()->GetGPUVirtualAddress() +
            static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(mCurrentObjectCBIndex) *
            mCurrFrameResource->ObjectCB->ElementByteSize();
        mCommandList->SetGraphicsRootConstantBufferView(1, objectCBAddress);

        mCommandList->DrawInstanced(mColliderBoxVertexCount, 1, 0, 0);
    };

    if (mode == ColliderBoundsDebugMode::SelectedOnly)
    {
        drawEntity(selectedEntity);
    }
    else
    {
        auto view = registry.view<TransformComponent, ColliderComponent>();
        for (auto entity : view)
            drawEntity(entity);
    }

    mCommandList->SetPipelineState(mPSOs["opaque"].Get());
    mCommandList->SetGraphicsRootSignature(mRootSignatures["standard"].Get());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// -------------------------------------------------------------
// MESH UPLOAD
// -------------------------------------------------------------

void D3DRenderAdapter::SetResourceManager(ResourceManager* resourceManager)
{
    mResourceManager = resourceManager;
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

    if (cpuMesh.vertices.empty() || cpuMesh.indices.empty())
    {
        throw std::runtime_error("Mesh has no vertices or indices");
    }

    auto meshGPU = std::make_unique<MeshGPU>();

    // Upload vertex buffer
    UINT64 vbByteSize = (UINT64)cpuMesh.vertices.size() * sizeof(Vertex);
    ComPtr<ID3D12Resource> vbUploadBuffer;

    meshGPU->vertexBuffer = d3dUtils::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        cpuMesh.vertices.data(),
        vbByteSize,
        vbUploadBuffer);

    // Upload index buffer
    UINT64 ibByteSize = (UINT64)cpuMesh.indices.size() * sizeof(uint32_t);
    ComPtr<ID3D12Resource> ibUploadBuffer;

    meshGPU->indexBuffer = d3dUtils::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        cpuMesh.indices.data(),
        ibByteSize,
        ibUploadBuffer);

    // Create vertex buffer view
    meshGPU->vbView.BufferLocation = meshGPU->vertexBuffer->GetGPUVirtualAddress();
    meshGPU->vbView.StrideInBytes = sizeof(Vertex);
    meshGPU->vbView.SizeInBytes = (UINT)vbByteSize;

    // Create index buffer view
    meshGPU->ibView.BufferLocation = meshGPU->indexBuffer->GetGPUVirtualAddress();
    meshGPU->ibView.Format = DXGI_FORMAT_R32_UINT;
    meshGPU->ibView.SizeInBytes = (UINT)ibByteSize;

    // Store index count
    meshGPU->indexCount = (UINT)cpuMesh.indices.size();

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

    // Store upload buffers in the mesh (will be disposed after GPU finishes)
    meshGPU->vertexUploadBuffer = vbUploadBuffer;
    meshGPU->indexUploadBuffer = ibUploadBuffer;
    meshGPU->uploadCompleteFence = mCurrentFence + 1;  // Will be signaled after next flush

    // Store in geometry map
    MeshGPU* result = meshGPU.get();
    mGeometries[meshId] = std::move(meshGPU);

    return result;
}

MeshGPU* D3DRenderAdapter::GetMeshGPU(MeshID meshId)
{
    // Return if already uploaded
    auto it = mGeometries.find(meshId);
    if (it != mGeometries.end())
    {
        return it->second.get();
    }

    // Lazy load
    return UploadMesh(meshId);
}

void D3DRenderAdapter::DrawMesh(MeshID meshId)
{
	MeshGPU* meshGPU = GetMeshGPU(meshId);
	if (!meshGPU || meshGPU->submeshes.empty())
	{
		//::OutputDebugStringA(L"Mesh not found or has no submeshes: " + std::to_wstring(meshId) + L"\n");
		return;
	}

	// Set graphics pipeline state and root signature (must be done before drawing)
	//mCommandList->SetPipelineState(mPSOs["opaque"].Get());
	//mCommandList->SetGraphicsRootSignature(mRootSignatures["standard"].Get());

	// Bind vertex and index buffers
	mCommandList->IASetVertexBuffers(0, 1, &meshGPU->vbView);
	mCommandList->IASetIndexBuffer(&meshGPU->ibView);

	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	D3D12_GPU_VIRTUAL_ADDRESS objectCBAddress =
		mCurrFrameResource->ObjectCB->Resource()->GetGPUVirtualAddress() +
		static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(mCurrentObjectCBIndex) *
		mCurrFrameResource->ObjectCB->ElementByteSize();
	mCommandList->SetGraphicsRootConstantBufferView(2, objectCBAddress);

	// Draw all submeshes with their respective materials
	for (size_t i = 0; i < meshGPU->submeshes.size(); ++i)
	{
		const auto& submesh = meshGPU->submeshes[i];

		// Get or load material (this will lazily load textures on first use)
		MaterialGPU* matGPU = GetOrLoadMaterial(submesh.material);

		// Bind textures if available
		if (matGPU)
		{
			// Bind descriptor heap for texture access
			ID3D12DescriptorHeap* heaps[] = { mCbvSrvUavHeap.Get() };
			mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

			if (matGPU->DiffuseSrvHeapIndex >= 0)
			{
				D3D12_GPU_DESCRIPTOR_HANDLE diffuseSRV = GetGPUDescriptorHandle(matGPU->DiffuseSrvHeapIndex);
				mCommandList->SetGraphicsRootDescriptorTable(0, diffuseSRV);
			}

			if (matGPU->NormalSrvHeapIndex >= 0)
			{
				D3D12_GPU_DESCRIPTOR_HANDLE normalSRV = GetGPUDescriptorHandle(matGPU->NormalSrvHeapIndex);
				mCommandList->SetGraphicsRootDescriptorTable(1, normalSRV);
			}

			// Set material for this submesh
			SetMaterial(submesh.material);
		}

		// Draw this submesh
		mCommandList->DrawIndexedInstanced(
			submesh.indexCount,    // IndexCountPerInstance
			1,                      // InstanceCount
			submesh.indexOffset,   // StartIndexLocation
			0,                      // BaseVertexLocation
			0                       // StartInstanceLocation
		);
	}
}

void D3DRenderAdapter::DrawSubmesh(MeshID meshId, uint32_t submeshIndex)
{
    MeshGPU* meshGPU = GetMeshGPU(meshId);
    if (!meshGPU || submeshIndex >= meshGPU->submeshes.size())
    {
        return;
    }

    // Set graphics pipeline state and root signature (must be done before drawing)
    mCommandList->SetPipelineState(mPSOs["opaque"].Get());
    mCommandList->SetGraphicsRootSignature(mRootSignatures["standard"].Get());

    const auto& submesh = meshGPU->submeshes[submeshIndex];

    // Bind vertex and index buffers
    mCommandList->IASetVertexBuffers(0, 1, &meshGPU->vbView);
    mCommandList->IASetIndexBuffer(&meshGPU->ibView);
    D3D12_GPU_VIRTUAL_ADDRESS objectCBAddress =
        mCurrFrameResource->ObjectCB->Resource()->GetGPUVirtualAddress() +
        static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(mCurrentObjectCBIndex) *
        mCurrFrameResource->ObjectCB->ElementByteSize();
    mCommandList->SetGraphicsRootConstantBufferView(2, objectCBAddress);

    // Get or load material (this will lazily load textures on first use)
    MaterialGPU* matGPU = GetOrLoadMaterial(submesh.material);

    // Bind textures if available
    if (matGPU)
    {
        // Bind descriptor heap for texture access
        ID3D12DescriptorHeap* heaps[] = { mCbvSrvUavHeap.Get() };
        mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

        if (matGPU->DiffuseSrvHeapIndex >= 0)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE diffuseSRV = GetGPUDescriptorHandle(matGPU->DiffuseSrvHeapIndex);
            mCommandList->SetGraphicsRootDescriptorTable(0, diffuseSRV);
        }

        if (matGPU->NormalSrvHeapIndex >= 0)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE normalSRV = GetGPUDescriptorHandle(matGPU->NormalSrvHeapIndex);
            mCommandList->SetGraphicsRootDescriptorTable(1, normalSRV);
        }

        // Set material for this submesh
        SetMaterial(submesh.material);
    }

    // Draw this specific submesh
    mCommandList->DrawIndexedInstanced(
        submesh.indexCount,     // IndexCountPerInstance
        1,                      // InstanceCount
        submesh.indexOffset,    // StartIndexLocation
        0,                      // BaseVertexLocation
        0                       // StartInstanceLocation
    );
}

int D3DRenderAdapter::LoadTexture(const std::wstring& filename)
{
	std::wstring fn = L"../../Textures/" + filename; // Assuming textures are in this relative path
    // Check if texture already loaded
    std::string filenameStr;
    size_t size = WideCharToMultiByte(CP_UTF8, 0, fn.c_str(), -1, nullptr, 0, nullptr, nullptr);
    filenameStr.resize(size - 1);
    WideCharToMultiByte(CP_UTF8, 0, fn.c_str(), -1, &filenameStr[0], size, nullptr, nullptr);
	//filenameStr = "../Textures/" + filenameStr; // Assuming textures are in this relative path

    auto it = mTextures.find(filenameStr);
    if (it != mTextures.end())
    {
        // Already loaded, return stored SRV index
        return it->second->SrvHeapIndex;
    }

    if (mNextCbvSrvIndex >= mCbvSrvUavDescriptorCount)
    {
        return -1; // Descriptor heap full
    }

    auto textureGPU = std::make_unique<TextureGPU>();
    textureGPU->Filename = fn;
    textureGPU->Name = filenameStr;

    // Load DDS texture using DirectX helper
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> uploadHeap;

    HRESULT hr = DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(),
        mCommandList.Get(),
        fn.c_str(),
        texture,
        uploadHeap);

    if (FAILED(hr))
    {
        return -1; // Failed to load texture
    }

    textureGPU->Resource = texture;
    textureGPU->UploadHeap = uploadHeap;

    // Create SRV for the texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = texture->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = (UINT)texture->GetDesc().MipLevels;

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        mCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
        mNextCbvSrvIndex,
        mCbvSrvUavDescriptorSize);

    md3dDevice->CreateShaderResourceView(texture.Get(), &srvDesc, handle);

    int srvHeapIndex = mNextCbvSrvIndex;
    mNextCbvSrvIndex++;

    // Store SRV index in TextureGPU
    textureGPU->SrvHeapIndex = srvHeapIndex;

    // Store texture
    mTextures[filenameStr] = std::move(textureGPU);

    // Keep upload heap alive in owned resources
    mOwnedResources.push_back(uploadHeap);

    return srvHeapIndex;
}

MaterialGPU* D3DRenderAdapter::GetOrLoadMaterial(MaterialID materialId)
{
    if (!mResourceManager)
        return nullptr;

    std::string matKey = std::to_string((uint32_t)materialId);
    auto it = mMaterials.find(matKey);
    if (it != mMaterials.end())
    {
        return it->second.get();
    }

    Material& cpuMaterial = mResourceManager->GetMaterial(materialId);

    auto materialGPU = std::make_unique<MaterialGPU>();
    materialGPU->Name = cpuMaterial.name;
    materialGPU->DiffuseAlbedo = DirectX::XMFLOAT4(
        cpuMaterial.color.x,
        cpuMaterial.color.y,
        cpuMaterial.color.z,
        1.0f);
    materialGPU->FresnelR0 = DirectX::XMFLOAT3(0.1f, 0.1f, 0.1f);
    materialGPU->Roughness = cpuMaterial.roughness;

    MaterialGPU* matGPU = materialGPU.get();

    if (cpuMaterial.albedo != 0)
    {
        try
        {
            Texture& cpuTexture = mResourceManager->GetTexture(cpuMaterial.albedo);
            if (!cpuTexture.filename.empty())
            {
                int srvIndex = LoadTexture(cpuTexture.filename);
                if (srvIndex >= 0)
                {
                    matGPU->DiffuseSrvHeapIndex = srvIndex;
                }
            }
        }
        catch (...)
        {
            // Texture not found or loading failed - leave as -1
        }
    }

    if (cpuMaterial.normal != 0)
    {
        try
        {
            Texture& cpuTexture = mResourceManager->GetTexture(cpuMaterial.normal);
            if (!cpuTexture.filename.empty())
            {
                int srvIndex = LoadTexture(cpuTexture.filename);
                if (srvIndex >= 0)
                {
                    matGPU->NormalSrvHeapIndex = srvIndex;
                }
            }
        }
        catch (...)
        {
            // Texture not found or loading failed - leave as -1
        }
    }

    if (matGPU->DiffuseSrvHeapIndex < 0)
    {
        matGPU->DiffuseSrvHeapIndex = LoadTexture(L"white1x1.dds");
    }

    if (matGPU->NormalSrvHeapIndex < 0)
    {
        matGPU->NormalSrvHeapIndex = LoadTexture(L"default_nmap.dds");
    }

    mMaterials[matKey] = std::move(materialGPU);
    return matGPU;
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
