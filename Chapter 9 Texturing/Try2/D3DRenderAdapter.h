#pragma once
#include "Commons.h"
#include "d3dUtils.h"

using Microsoft::WRL::ComPtr;

class D3DRenderAdapter : public IRenderAdapter
{
public:
    void Init(void* windowHandle, uint32_t width, uint32_t height) override;
    void BeginFrame() override;
    void EndFrame() override;

    void SetTransform(const glm::mat4& world) override;

    void SetMaterial(MaterialID material) override;

    void DrawIndexed(
        uint32_t indexCount,
        uint32_t startIndex,
        int32_t baseVertex
    ) override;

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

    D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
};
