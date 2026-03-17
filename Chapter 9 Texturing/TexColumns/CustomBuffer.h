#pragma once

#include <d3d12.h>
#include "d3dx12.h"
#include <wrl.h>
#include <array>
#include <vector>
using Microsoft::WRL::ComPtr;

class CustomBuffer
{
public:
	UINT NumTextures;
    UINT SrvHeapStartIndex;

	//
	std::vector<ComPtr<ID3D12Resource>> Textures;

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> RTVlist;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> SRVlist;

    // Инициализация всех ресурсов
    CustomBuffer(UINT NumTextures);
    void Initialize(ID3D12Device* device, UINT width, UINT height,
        DXGI_FORMAT* formats,
        D3D12_CPU_DESCRIPTOR_HANDLE* rtvHandles,
        D3D12_CPU_DESCRIPTOR_HANDLE* srvHandles);
    void TransitionState(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);
    void SetRenderTargets(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE dsv);
    void ClearRenderTargets(ID3D12GraphicsCommandList* cmdList, const float clearColor[4]);


    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVTable(ID3D12DescriptorHeap* heap, UINT descriptorSize) const;

private:
    void CreateRenderTarget(ID3D12Device* device, UINT width, UINT height,
        DXGI_FORMAT format,
        ComPtr<ID3D12Resource>& outResource,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);
};

