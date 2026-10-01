#include "TextureUploader.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <tracy/Tracy.hpp>

namespace {
void Check(HRESULT hr, const char* message) {
    if (FAILED(hr)) throw std::runtime_error(std::string(message) + " (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ")");
}
bool BlockCompressed(DXGI_FORMAT f) {
    return (f >= DXGI_FORMAT_BC1_TYPELESS && f <= DXGI_FORMAT_BC5_SNORM) ||
        (f >= DXGI_FORMAT_BC6H_TYPELESS && f <= DXGI_FORMAT_BC7_UNORM_SRGB);
}
}
void TextureUploader::Init(ID3D12Device* d, ID3D12DescriptorHeap* h, UINT first, UINT count)
{
    if (device || !d || !h || count < 3 || uint64_t(first) + count > h->GetDesc().NumDescriptors)
        throw std::invalid_argument("Invalid texture descriptor range");
    device = d; heap = h;
    stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (UINT i = first + count; i > first;) freeSlots.push_back(--i);
}
UINT TextureUploader::CreateView(ID3D12Resource* resource)
{
    if (freeSlots.empty()) throw std::runtime_error("Texture descriptor capacity exhausted");
    const UINT slot = freeSlots.back(); freeSlots.pop_back();
    D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Format = resource->GetDesc().Format;
    view.Texture2D.MipLevels = resource->GetDesc().MipLevels;
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += SIZE_T(slot) * stride;
    device->CreateShaderResourceView(resource, &view, handle);
    ++stats.descriptors;
    return slot;
}
void TextureUploader::CreatePlaceholders(ID3D12GraphicsCommandList* list)
{
    const uint8_t colors[3][4] = { {255,255,255,255}, {128,128,255,255}, {255,0,255,255} };
    for (int i = 0; i < 3; ++i) {
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1);
        auto gpuHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        Check(device->CreateCommittedResource(&gpuHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&placeholders[i])), "Create fallback texture");
        TexturePixels pixels;
        pixels.image.desc = desc;
        pixels.image.subresources.push_back({ colors[i], 4, 4 });
        Entry entry; entry.resource = placeholders[i];
        if (!CopyChunk(entry, pixels, list)) throw std::runtime_error("Upload budget too small for fallbacks");
        placeholderSlots[i] = static_cast<int>(CreateView(placeholders[i].Get()));
    }
}
bool TextureUploader::CopyChunk(Entry& entry, const TexturePixels& pixels, ID3D12GraphicsCommandList* list)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 rowBytes = 0, total = 0;
    device->GetCopyableFootprints(&pixels.image.desc, entry.mip, 1, 0, &footprint, &rows, &rowBytes, &total);
    const auto& source = pixels.image.subresources.at(entry.mip);
    if (footprint.Footprint.RowPitch > limits.bytesPerFrame || footprint.Footprint.RowPitch > limits.stagingBytes)
        throw std::runtime_error("Upload budget cannot fit one texture row");
    if (total == UINT64_MAX || !rows || footprint.Footprint.Depth != 1 ||
        rowBytes > static_cast<UINT64>(source.RowPitch) || uint64_t(rows) * source.RowPitch > static_cast<UINT64>(source.SlicePitch))
        throw std::runtime_error("Unsupported texture footprint");
    uint64_t available = (std::min)(limits.bytesPerFrame - (std::min)(stats.frameBytes, limits.bytesPerFrame),
        limits.stagingBytes - (std::min)(stats.stagingBytes, limits.stagingBytes));
    available = std::min<uint64_t>(available, 1024 * 1024); // Bounded memcpy/driver work per chunk.
    const UINT copyRows = static_cast<UINT>(std::min<uint64_t>(rows - entry.row, available / footprint.Footprint.RowPitch));
    if (!copyRows) return false;
    const UINT blockHeight = BlockCompressed(pixels.image.desc.Format) ? 4 : 1;
    footprint.Offset = 0;
    footprint.Footprint.Height = (std::min)(footprint.Footprint.Height - entry.row * blockHeight, copyRows * blockHeight);
    const uint64_t bufferBytes = uint64_t(footprint.Footprint.RowPitch) * copyRows;
    auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferBytes);
    ResourcePtr upload;
    Check(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&upload)), "Create texture staging buffer");
    uint8_t* mapped = nullptr;
    D3D12_RANGE noRead = {0, 0};
    Check(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)), "Map texture staging buffer");
    for (UINT row = 0; row < copyRows; ++row)
        memcpy(mapped + uint64_t(row) * footprint.Footprint.RowPitch,
            static_cast<const uint8_t*>(source.pData) + uint64_t(entry.row + row) * source.RowPitch, static_cast<size_t>(rowBytes));
    upload->Unmap(0, nullptr);
    staging.push_back({ upload, bufferBytes }); // Retain before recording the resource reference.
    stats.stagingBytes += bufferBytes;
    stats.frameBytes += bufferBytes;
    CD3DX12_TEXTURE_COPY_LOCATION dst(entry.resource.Get(), entry.mip);
    CD3DX12_TEXTURE_COPY_LOCATION src(upload.Get(), footprint);
    list->CopyTextureRegion(&dst, 0, entry.row * blockHeight, 0, &src, nullptr);
    entry.row += copyRows;
    if (entry.row == rows) { entry.row = 0; ++entry.mip; }
    if (entry.mip == pixels.image.desc.MipLevels) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(entry.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &barrier);
        entry.allCopied = true;
    }
    return true;
}
void TextureUploader::Pump(TextureLoader& loader, ID3D12GraphicsCommandList* list, uint64_t completedFence)
{
    ZoneScopedN("Texture GPU upload pump");
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
    stats.frameBytes = 0;
    std::erase_if(staging, [&](const Staging& s) {
        if (s.fence && s.fence <= completedFence) { stats.stagingBytes -= s.bytes; return true; }
        return false;
    });
    std::erase_if(retired, [&](const Retired& r) {
        if (r.fence && r.fence <= completedFence) {
            freeSlots.push_back(r.slot); --stats.descriptors; stats.residentBytes -= r.bytes; return true;
        }
        return false;
    });
    for (auto it = entries.begin(); it != entries.end();) {
        auto& tex = loader.Get(it->first);
        auto& e = it->second;
        if (tex.generation != e.generation || tex.state == TextureState::Failed || tex.state == TextureState::Cancelled) {
            retired.push_back({ std::move(e.resource), e.bytes, 0, e.slot });
            it = entries.erase(it);
        } else {
            if (e.finalFence && e.finalFence <= completedFence) tex.state = TextureState::Ready;
            ++it;
        }
    }
    if (!placeholders[0]) CreatePlaceholders(list);
    loader.Tick();
    for (unsigned i = 0; i < limits.startsPerFrame && elapsed() < limits.millisecondsPerFrame; ++i) {
        const auto id = loader.PopReady();
        if (!id) break;
        auto& tex = loader.Get(id);
        try {
            const auto& desc = tex.pixels->image.desc;
            D3D12_FEATURE_DATA_FORMAT_INFO info = { desc.Format, 0 };
            Check(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info)), "Check texture format");
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support = { desc.Format };
            Check(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)), "Check texture sampling");
            if (info.PlaneCount != 1 || !(support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE))
                throw std::runtime_error("Texture format cannot be sampled by a 2D material");
            auto allocation = device->GetResourceAllocationInfo(0, 1, &desc);
            if (allocation.SizeInBytes == UINT64_MAX || allocation.SizeInBytes > limits.residentBytes - (std::min)(stats.residentBytes, limits.residentBytes))
                throw std::runtime_error("Texture GPU memory budget exhausted");
            if (freeSlots.empty()) throw std::runtime_error("Texture descriptor capacity exhausted");
            Entry entry;
            auto gpuHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            Check(device->CreateCommittedResource(&gpuHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&entry.resource)), "Create GPU texture");
            entry.generation = tex.generation;
            entry.bytes = allocation.SizeInBytes;
            entry.slot = CreateView(entry.resource.Get());
            stats.residentBytes += entry.bytes;
            entries.emplace(id, std::move(entry));
            tex.state = TextureState::UploadingGPU;
        } catch (const std::exception& e) { loader.Fail(id, e.what()); }
    }
    for (auto& [id, entry] : entries) {
        if (entry.allCopied) continue;
        auto& tex = loader.Get(id);
        if (tex.state != TextureState::UploadingGPU) continue;
        try {
            while (!entry.allCopied && elapsed() < limits.millisecondsPerFrame) {
                if (!CopyChunk(entry, *tex.pixels, list)) break;
            }
            if (entry.allCopied) loader.ReleaseCPU(id);
        } catch (const std::exception& e) { loader.Fail(id, e.what()); }
        if (elapsed() >= limits.millisecondsPerFrame || stats.frameBytes >= limits.bytesPerFrame) break;
    }
    stats.pumpMilliseconds = elapsed();
    TracyPlot("Texture staging MiB", static_cast<double>(stats.stagingBytes) / (1024 * 1024));
    TracyPlot("Texture upload MiB/frame", static_cast<double>(stats.frameBytes) / (1024 * 1024));
}
void TextureUploader::OnSubmitted(uint64_t fence)
{
    for (auto& s : staging) if (!s.fence) s.fence = fence;
    for (auto& r : retired) if (!r.fence) r.fence = fence;
    for (auto& [id, e] : entries) if (e.allCopied && !e.finalFence) e.finalFence = fence;
}
int TextureUploader::Resolve(const TextureLoader& loader, TextureHandle id, bool normal) const
{
    if (!id) return placeholderSlots[normal ? 1 : 0];
    const auto& tex = loader.Get(id);
    if (tex.state == TextureState::Ready) {
        auto it = entries.find(id);
        if (it != entries.end() && it->second.generation == tex.generation) return static_cast<int>(it->second.slot);
    }
    return placeholderSlots[normal ? 1 : (tex.state == TextureState::Failed ? 2 : 0)];
}
ID3D12Resource* TextureUploader::Resource(TextureHandle id) const
{
    auto it = entries.find(id);
    return it == entries.end() ? nullptr : it->second.resource.Get();
}
