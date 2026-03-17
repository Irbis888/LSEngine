#include "CustomBuffer.h"
#include <string>


CustomBuffer::CustomBuffer(UINT NumTextures) {
    this->NumTextures = NumTextures;
}

void CustomBuffer::Initialize(ID3D12Device* device, UINT width, UINT height,
    DXGI_FORMAT* formats,
    D3D12_CPU_DESCRIPTOR_HANDLE* rtvHandles,
    D3D12_CPU_DESCRIPTOR_HANDLE* srvHandles)
{

    for (int i = 0; i < this->NumTextures; i++) {
        Textures.push_back(nullptr);
        CreateRenderTarget(device, width, height, formats[i], Textures[i], rtvHandles[i], srvHandles[i]);
        std::wstring wstr = std::to_wstring(this->NumTextures);
        LPCWSTR ptr = wstr.c_str();
        OutputDebugString(ptr);

        RTVlist.push_back(rtvHandles[i]);
        SRVlist.push_back(srvHandles[i]);
    }
}


void CustomBuffer::TransitionState(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    for (int i = 0; i < this->NumTextures; i++) {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(Textures[i].Get(), from, to));
    }
    cmdList->ResourceBarrier(this->NumTextures, barriers.data());
    
}

void CustomBuffer::SetRenderTargets(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
    cmdList->OMSetRenderTargets(this->NumTextures, RTVlist.data(), false, &dsv);
}

void CustomBuffer::ClearRenderTargets(ID3D12GraphicsCommandList* cmdList, const float clearColor[4])
{
    for (int i = 0; i < this->NumTextures; i++) {
        cmdList->ClearRenderTargetView(RTVlist[i], clearColor, 0, nullptr);
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE CustomBuffer::GetSRVTable(ID3D12DescriptorHeap* heap, UINT descriptorSize) const
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        heap->GetGPUDescriptorHandleForHeapStart(),
        SrvHeapStartIndex,
        descriptorSize
    );
}

void CustomBuffer::CreateRenderTarget(ID3D12Device* device, UINT width, UINT height,
    DXGI_FORMAT format,
    ComPtr<ID3D12Resource>& outResource,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = format;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 0.0f;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&outResource));

    // RTV
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    device->CreateRenderTargetView(outResource.Get(), &rtvDesc, rtvHandle);

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(outResource.Get(), &srvDesc, srvHandle);
}