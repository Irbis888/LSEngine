#include "D3DRenderAdapter.h"
#include <algorithm>
#include <cstring>

void D3DRenderAdapter::CollectEditorTargets()
{
    const UINT64 completedFence = mFence->GetCompletedValue();
    while (!mRetiredEditorTargets.empty() && mRetiredEditorTargets.front()->retireFence <= completedFence)
    {
        mFreeEditorDescriptors.push_back(mRetiredEditorTargets.front()->srvIndex);
        mRetiredEditorTargets.pop_front();
    }
}

uint64_t D3DRenderAdapter::ResizeEditorViewport(uint32_t width, uint32_t height)
{
    if (!width || !height)
    {
        return 0;
    }
    width = std::min(width, 8192u);
    height = std::min(height, 8192u);
    CollectEditorTargets();
    if (mEditorTarget && mEditorTarget->width == width && mEditorTarget->height == height)
    {
        return GetGPUDescriptorHandle(mEditorTarget->srvIndex).ptr;
    }
    auto target = std::make_unique<EditorTarget>();
    target->width = width;
    target->height = height;
    // Reserved range below the engine's allocator (1024), separate from ImGui (900..963).
    if (mFreeEditorDescriptors.empty())
    {
        for (UINT descriptor = 980; descriptor < 996; ++descriptor)
        {
            bool used = mEditorTarget && mEditorTarget->srvIndex == descriptor;
            for (const auto& retired : mRetiredEditorTargets)
            {
                used |= retired->srvIndex == descriptor;
            }
            if (!used)
            {
                mFreeEditorDescriptors.push_back(descriptor);
            }
        }
    }
    if (mFreeEditorDescriptors.empty())
    {
        // Keep the previous image until a fence releases a slot. Never wait for resize.
        return mEditorTarget ? GetGPUDescriptorHandle(mEditorTarget->srvIndex).ptr : 0;
    }
    target->srvIndex = mFreeEditorDescriptors.back();
    mFreeEditorDescriptors.pop_back();
    D3D12_DESCRIPTOR_HEAP_DESC heapDescription = {};
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDescription.NumDescriptors = 2;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&target->rtvHeap)));
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDescription.NumDescriptors = 1;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&target->dsvHeap)));
    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto textureDescription = CD3DX12_RESOURCE_DESC::Tex2D(mBackBufferFormat, width, height, 1, 1);
    textureDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = mBackBufferFormat;
    ThrowIfFailed(md3dDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
        &textureDescription, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&target->color)));
    textureDescription.Format = DXGI_FORMAT_R32_UINT;
    clearValue.Format = DXGI_FORMAT_R32_UINT;
    ThrowIfFailed(md3dDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
        &textureDescription, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&target->identifiers)));
    textureDescription.Format = mDepthStencilFormat;
    textureDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    clearValue.Format = mDepthStencilFormat;
    clearValue.DepthStencil.Depth = 1.0f;
    ThrowIfFailed(md3dDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
        &textureDescription, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&target->depth)));
    auto colorView = target->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    auto identifierView = CD3DX12_CPU_DESCRIPTOR_HANDLE(colorView, 1, mRtvDescriptorSize);
    md3dDevice->CreateRenderTargetView(target->color.Get(), nullptr, colorView);
    md3dDevice->CreateRenderTargetView(target->identifiers.Get(), nullptr, identifierView);
    md3dDevice->CreateDepthStencilView(target->depth.Get(), nullptr, target->dsvHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC shaderView = {};
    shaderView.Format = mBackBufferFormat;
    shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shaderView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shaderView.Texture2D.MipLevels = 1;
    auto descriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), target->srvIndex, mCbvSrvUavDescriptorSize);
    md3dDevice->CreateShaderResourceView(target->color.Get(), &shaderView, descriptor);
    if (mEditorTarget)
    {
        mEditorTarget->retireFence = mCurrentFence + 1;
        mRetiredEditorTargets.push_back(std::move(mEditorTarget));
    }
    mEditorTarget = std::move(target);
    return GetGPUDescriptorHandle(mEditorTarget->srvIndex).ptr;
}

void D3DRenderAdapter::BeginScene(bool editor, bool visible)
{
    mEditorScene = editor;
    mSceneVisible = visible && (!editor || mEditorTarget != nullptr);
    mFrameEntities.clear();
    mFrameEntities.push_back(entt::null);
    mRenderEntityID = 0;
    if (!mSceneVisible || !editor)
    {
        return;
    }
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mEditorTarget->color.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrier);
    auto renderView = mEditorTarget->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    auto depthView = mEditorTarget->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    mCommandList->OMSetRenderTargets(2, &renderView, TRUE, &depthView);
    const float background[] = {0.1f, 0.2f, 0.4f, 1.0f};
    const float emptyIdentifiers[] = {0, 0, 0, 0};
    mCommandList->ClearRenderTargetView(renderView, background, 0, nullptr);
    auto identifierView = CD3DX12_CPU_DESCRIPTOR_HANDLE(renderView, 1, mRtvDescriptorSize);
    mCommandList->ClearRenderTargetView(identifierView, emptyIdentifiers, 0, nullptr);
    mCommandList->ClearDepthStencilView(depthView, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1, 0, 0, nullptr);
    D3D12_VIEWPORT viewport = {0, 0, float(mEditorTarget->width), float(mEditorTarget->height), 0, 1};
    D3D12_RECT scissor = {0, 0, LONG(mEditorTarget->width), LONG(mEditorTarget->height)};
    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissor);
}

void D3DRenderAdapter::SetRenderEntity(entt::entity entity)
{
    if (mEditorScene)
    {
        mRenderEntityID = static_cast<uint32_t>(mFrameEntities.size());
        mFrameEntities.push_back(entity);
    }
}

void D3DRenderAdapter::RequestViewportPick(uint32_t x, uint32_t y, uint64_t request, uint64_t sceneGeneration)
{
    mPickX = x;
    mPickY = y;
    mPendingPick = {request, sceneGeneration, entt::null};
}

void D3DRenderAdapter::EndScene()
{
    if (mEditorScene && mSceneVisible)
    {
        auto colorBarrier = CD3DX12_RESOURCE_BARRIER::Transition(mEditorTarget->color.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        mCommandList->ResourceBarrier(1, &colorBarrier);
        if (mPendingPick.request && mPickReadbacks.size() < 4)
        {
            PickReadback readback;
            readback.result = mPendingPick;
            readback.entities = mFrameEntities;
            readback.fence = mCurrentFence + 1;
            const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
            const auto bufferDescription = CD3DX12_RESOURCE_DESC::Buffer(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            ThrowIfFailed(md3dDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                &bufferDescription, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback.buffer)));
            auto identifierBarrier = CD3DX12_RESOURCE_BARRIER::Transition(mEditorTarget->identifiers.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            mCommandList->ResourceBarrier(1, &identifierBarrier);
            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = mEditorTarget->identifiers.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = readback.buffer.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_UINT, 1, 1, 1, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT};
            const UINT pixelX = std::min(mPickX, mEditorTarget->width - 1);
            const UINT pixelY = std::min(mPickY, mEditorTarget->height - 1);
            D3D12_BOX pixelBox = {pixelX, pixelY, 0, pixelX + 1, pixelY + 1, 1};
            mCommandList->CopyTextureRegion(&destination, 0, 0, 0, &source, &pixelBox);
            identifierBarrier = CD3DX12_RESOURCE_BARRIER::Transition(mEditorTarget->identifiers.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            mCommandList->ResourceBarrier(1, &identifierBarrier);
            mPickReadbacks.push_back(std::move(readback));
        }
    }
    mPendingPick = {};
    auto renderView = CurrentBackBufferView();
    mCommandList->OMSetRenderTargets(1, &renderView, TRUE, nullptr);
    if (mEditorScene)
    {
        const float editorBackground[] = {0.06f, 0.065f, 0.075f, 1};
        mCommandList->ClearRenderTargetView(renderView, editorBackground, 0, nullptr);
    }
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);
}

bool D3DRenderAdapter::PollViewportPick(ViewportPickResult& result)
{
    if (mPickReadbacks.empty() || mPickReadbacks.front().fence > mFence->GetCompletedValue())
    {
        return false;
    }
    auto& readback = mPickReadbacks.front();
    D3D12_RANGE readRange = {0, sizeof(uint32_t)};
    void* pixels = nullptr;
    ThrowIfFailed(readback.buffer->Map(0, &readRange, &pixels));
    uint32_t identifier = 0;
    std::memcpy(&identifier, pixels, sizeof(identifier));
    const D3D12_RANGE writtenRange = {0, 0};
    readback.buffer->Unmap(0, &writtenRange);
    result = readback.result;
    if (identifier < readback.entities.size())
    {
        result.entity = readback.entities[identifier];
    }
    mPickReadbacks.pop_front();
    return true;
}
