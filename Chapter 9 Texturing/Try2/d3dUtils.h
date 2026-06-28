#pragma once
#include "Commons.h"

#include <windows.h>
#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <memory>
#include <algorithm>
#include <vector>
#include <array>
#include <unordered_map>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <cassert>
#include "d3dx12.h"
#include "DDSTextureLoader.h"
#include "../TexColumns/CustomBuffer.h"
#include "DDSTextureLoader.h"
#include <MathHelper.h>

class d3dUtils
{
public:

    static UINT CalcConstantBufferByteSize(UINT byteSize)
    {
        // Constant buffers must be a multiple of the minimum hardware
        // allocation size (usually 256 bytes).  So round up to nearest
        // multiple of 256.  We do this by adding 255 and then masking off
        // the lower 2 bytes which store all bits < 256.
        // Example: Suppose byteSize = 300.
        // (300 + 255) & ~255
        // 555 & ~255
        // 0x022B & ~0x00ff
        // 0x022B & 0xff00
        // 0x0200
        // 512
        return (byteSize + 255) & ~255;
    }

    static Microsoft::WRL::ComPtr<ID3DBlob> LoadBinary(const std::wstring& filename);

    static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const void* initData,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer);

    static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
        const std::wstring& filename,
        const D3D_SHADER_MACRO* defines,
        const std::string& entrypoint,
        const std::string& target);

    // Build a world transformation matrix from TransformComponent (TRS composition)
    // Returns result as XMFLOAT4X4 for direct use in ObjectConstants
    static DirectX::XMFLOAT4X4 TransformComponentToWorldMatrix(const TransformComponent& transform)
    {
        // Build scale matrix
        DirectX::XMMATRIX S = DirectX::XMMatrixScaling(transform.scale.x, transform.scale.y, transform.scale.z);

        // Build rotation matrix (applying rotations in X, Y, Z order)
        DirectX::XMMATRIX Rx = DirectX::XMMatrixRotationX(transform.rotation.x);
        DirectX::XMMATRIX Ry = DirectX::XMMatrixRotationY(transform.rotation.y);
        DirectX::XMMATRIX Rz = DirectX::XMMatrixRotationZ(transform.rotation.z);
        DirectX::XMMATRIX R = Rx * Ry * Rz;

        // Build translation matrix
        DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(transform.position.x, transform.position.y, transform.position.z);

        // Combine: S * R * T
        DirectX::XMMATRIX world = S * R * T;

        // Store in XMFLOAT4X4 and return
        DirectX::XMFLOAT4X4 result;
        DirectX::XMStoreFloat4x4(&result, world);
        return result;
    }

    // Compute inverse of a 4x4 matrix (for InvWorld)
    static DirectX::XMFLOAT4X4 InvertMatrix4x4(const DirectX::XMFLOAT4X4& inMatrix)
    {
        DirectX::XMMATRIX xmIn = DirectX::XMLoadFloat4x4(&inMatrix);
        DirectX::XMVECTOR det = DirectX::XMMatrixDeterminant(xmIn);
        DirectX::XMMATRIX xmInv = DirectX::XMMatrixInverse(&det, xmIn);

        DirectX::XMFLOAT4X4 out;
        DirectX::XMStoreFloat4x4(&out, xmInv);
        return out;
    }
};

inline std::wstring AnsiToWString(const std::string& str)
{
    WCHAR buffer[512];
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
    return std::wstring(buffer);
}

struct SubmeshGPU
{
    uint32_t indexOffset;
    uint32_t indexCount;
    uint32_t material;
};

struct MeshGPU
{
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;

    D3D12_VERTEX_BUFFER_VIEW vbView;
    D3D12_INDEX_BUFFER_VIEW ibView;

    UINT indexCount;

    std::vector<SubmeshGPU> submeshes;
    uint32_t materialVersion = 0;

    // Upload buffers (temporary, disposed after GPU finishes with them)
    ComPtr<ID3D12Resource> vertexUploadBuffer;
    ComPtr<ID3D12Resource> indexUploadBuffer;

    // Fence value when upload completes (used to know when buffers can be disposed)
    UINT64 uploadCompleteFence = 0;
};


struct MaterialGPU
{
    // Unique material name for lookup.
    std::string Name;

    // Index into constant buffer corresponding to this material.
    int MatCBIndex = -1;

    // Index into SRV heap for diffuse texture.
    int DiffuseSrvHeapIndex = -1;

    // Index into SRV heap for normal texture.
    int NormalSrvHeapIndex = -1;


    // Dirty flag indicating the material has changed and we need to update the constant buffer.
    // Because we have a material constant buffer for each FrameResource, we have to apply the
    // update to each FrameResource.  Thus, when we modify a material we should set 
    // NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
    int NumFramesDirty = 3;

    // Material constant buffer data used for shading.
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = .25f;
    DirectX::XMFLOAT4X4 MatTransform = DirectX::XMFLOAT4X4 (
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
};

struct TextureGPU
{
    // Unique material name for lookup.
    std::string Name;

    std::wstring Filename;

    Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr;

    // Index into SRV heap for this texture
    int SrvHeapIndex = -1;
};

struct Light
{
    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    float FalloffStart = 1.0f;                          // point/spot light only
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;                            // spot light only
};

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif

#ifndef ReleaseCom
#define ReleaseCom(x) { if(x){ x->Release(); x = 0; } }
#endif

