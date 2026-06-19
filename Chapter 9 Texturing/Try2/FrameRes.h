#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <cstring>
#include <stdexcept>
#include "d3dUtils.h"

using Microsoft::WRL::ComPtr;

// =========================================================================
// CONSTANT BUFFER STRUCTURES (GPU-aligned)
// =========================================================================

// Must match MaxLights in LightingUtil.hlsl
#define MaxLights 16

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();        // 4x4 matrix
    DirectX::XMFLOAT4X4 InvWorld = MathHelper::Identity4x4();     // 4x4 matrix
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4(); // 4x4 matrix for texture coordinates
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();         // 4x4 matrix
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();      // 4x4 matrix
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();         // 4x4 matrix
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();      // 4x4 matrix
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();     // 4x4 matrix
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();  // 4x4 matrix
    DirectX::XMFLOAT3 EyePosW;        // Camera position in world space
    float cbPerObjectPad1;
    DirectX::XMFLOAT2 RenderTargetSize;      // Render target size
    DirectX::XMFLOAT2 InvRenderTargetSize;   // Inverse render target size
    float NearZ;                      // Near plane distance
    float FarZ;                       // Far plane distance
    float TotalTime;                  // Total elapsed time
    float DeltaTime;                  // Frame delta time
    DirectX::XMFLOAT4 AmbientLight;   // Ambient light color (XMFLOAT4 matches GPU layout)
    Light Lights[MaxLights];          // Array of lights (max 16)
};

struct MaterialConstants
{
    DirectX::XMFLOAT4 DiffuseAlbedo;  // vec4 RGBA
    DirectX::XMFLOAT3 FresnelR0;      // vec3 for Fresnel reflection at 0 degrees
    float Roughness;                   // Roughness value for PBR
    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4(); // 4x4 matrix for material texture transform
};

// =========================================================================
// UPLOAD BUFFER TEMPLATE
// =========================================================================

template<typename T>
class UploadBuffer
{
public:
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
        mIsConstantBuffer(isConstantBuffer)
    {
        mElementByteSize = sizeof(T);

        // Constant buffer elements need to be multiples of 256 bytes.
        // This is because the hardware can only view constant data 
        // at m*256 byte offsets and of n*256 byte lengths. 
        // typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
        // UINT64 OffsetInBytes; // multiple of 256
        // UINT   SizeInBytes;   // multiple of 256
        // } D3D12_CONSTANT_BUFFER_VIEW_DESC;
        if (isConstantBuffer)
            mElementByteSize = (sizeof(T) + 255) & ~255;

        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mUploadBuffer)));

        ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));

        // We do not need to unmap until we are done with the resource.  However, we must not write to
        // the resource while it is in use by the GPU (so we must use synchronization techniques).
    }

    UploadBuffer(const UploadBuffer& rhs) = delete;
    UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
    ~UploadBuffer()
    {
        if (mUploadBuffer != nullptr)
            mUploadBuffer->Unmap(0, nullptr);

        mMappedData = nullptr;
    }

    ID3D12Resource* Resource()const
    {
        return mUploadBuffer.Get();
    }

    UINT ElementByteSize() const
    {
        return mElementByteSize;
    }

    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
    BYTE* mMappedData = nullptr;

    UINT mElementByteSize = 0;
    bool mIsConstantBuffer = false;
};
// =========================================================================
// FRAME RESOURCE CLASS
// =========================================================================

class FrameRes
{
public:
    FrameRes(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
    ~FrameRes() = default;

    // Resource access
    ComPtr<ID3D12CommandAllocator> CmdListAlloc;
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB;
    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB;

    // GPU synchronization
    UINT64 Fence = 0;
};
