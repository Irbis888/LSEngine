#pragma once
#include "TextureLoading.h"
#include <wrl.h>

struct TextureUploadStats
{
    uint64_t residentBytes = 0, stagingBytes = 0, frameBytes = 0;
    size_t descriptors = 0;
    double pumpMilliseconds = 0;
};
// All calls are render-thread only. Caller must drain its GPU queue before destruction.
class TextureUploader
{
public:
    struct Limits {
        uint64_t bytesPerFrame = 8ull * 1024 * 1024;
        uint64_t stagingBytes = 64ull * 1024 * 1024;
        uint64_t residentBytes = 512ull * 1024 * 1024;
        double millisecondsPerFrame = 2.0; // Soft deadline, checked between copy chunks.
        unsigned startsPerFrame = 4;
    } limits;
    void Init(ID3D12Device* device, ID3D12DescriptorHeap* heap, UINT first, UINT count);
    void Pump(TextureLoader& loader, ID3D12GraphicsCommandList* list, uint64_t completedFence);
    void OnSubmitted(uint64_t fence);
    int Resolve(const TextureLoader& loader, TextureHandle handle, bool normal) const;
    const TextureUploadStats& Stats() const { return stats; }
    // Diagnostics/tests only; callers must respect the texture's fence/state.
    ID3D12Resource* Resource(TextureHandle id) const;
private:
    using ResourcePtr = Microsoft::WRL::ComPtr<ID3D12Resource>;
    struct Entry {
        ResourcePtr resource;
        uint64_t generation = 0, bytes = 0, finalFence = 0;
        UINT slot = 0, mip = 0, row = 0;
        bool allCopied = false;
    };
    struct Staging { ResourcePtr resource; uint64_t bytes, fence = 0; };
    struct Retired { ResourcePtr resource; uint64_t bytes, fence = 0; UINT slot; };
    ID3D12Device* device = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    UINT stride = 0;
    std::vector<UINT> freeSlots;
    std::unordered_map<TextureHandle, Entry> entries;
    std::vector<Staging> staging;
    std::vector<Retired> retired;
    ResourcePtr placeholders[3];
    int placeholderSlots[3] = {-1, -1, -1};
    TextureUploadStats stats;
    void CreatePlaceholders(ID3D12GraphicsCommandList* list);
    UINT CreateView(ID3D12Resource* resource);
    bool CopyChunk(Entry& entry, const TexturePixels& pixels, ID3D12GraphicsCommandList* list);
};
