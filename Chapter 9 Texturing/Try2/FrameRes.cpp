#include "FrameRes.h"
#include <stdexcept>

FrameRes::FrameRes(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
    if (FAILED(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc.GetAddressOf()))))
    {
        throw std::runtime_error("Failed to create command allocator for FrameRes");
    }

    // Create constant buffer upload buffers for this frame
    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
    MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
}
