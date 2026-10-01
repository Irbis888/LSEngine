#pragma once
#include "JobSystem.h"
#include "DDSTextureLoader.h"
#include <atomic>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

using TextureHandle = uint32_t; // Stable ID; zero means no texture.
enum class TextureState { Queued, LoadingCPU, ReadyCPU, UploadingGPU, Ready, Failed, Cancelled };
enum class TextureColorSpace { File, Linear, SRGB };
struct TexturePixels
{
    std::vector<uint8_t> bytes;
    DirectX::PreparedDDS12 image;
};
struct Texture
{
    std::string name;
    std::wstring filename;
    TextureColorSpace colorSpace = TextureColorSpace::File;
    TextureState state = TextureState::Queued;
    uint64_t generation = 1;
    std::string error;
    bool active = true;
    std::unique_ptr<TexturePixels> pixels;
};
struct TextureLoadingStats
{
    size_t queued = 0, loading = 0, readyCPU = 0, uploading = 0, ready = 0, failed = 0, cancelled = 0;
    size_t reservedCPUBytes = 0;
};

// Owner-thread registry. Workers only access their private Work object.
class TextureLoader
{
public:
    static constexpr size_t MaxTextureBytes = 64ull * 1024 * 1024;
    static constexpr size_t MaxResidentCPUJobs = 4; // <= 256 MiB reserved, including completed results.
    TextureLoader() = default;
    ~TextureLoader();
    TextureHandle Request(const std::wstring& filename, TextureColorSpace color = TextureColorSpace::File);
    Texture& Get(TextureHandle id) { return textures.at(id); }
    const Texture& Get(TextureHandle id) const { return textures.at(id); }
    const auto& Records() const { return textures; }
    void Activate(TextureHandle id);
    void SetActive(const std::unordered_set<TextureHandle>& active);
    void Cancel(TextureHandle id);
    void Tick();
    TextureHandle PopReady();
    void ReleaseCPU(TextureHandle id);
    void Fail(TextureHandle id, const std::string& error);
    void Shutdown();
    TextureLoadingStats Stats() const;
    static std::unique_ptr<TexturePixels> Read(const std::wstring& filename, TextureColorSpace color,
        const std::atomic_bool& cancelled);
private:
    struct Work { std::atomic_bool cancelled{false}; std::unique_ptr<TexturePixels> result; };
    struct Flight { TextureHandle id; uint64_t generation; std::shared_ptr<Work> work; JobSystem::Handle job; };
    std::unordered_map<TextureHandle, Texture> textures;
    std::unordered_map<std::wstring, TextureHandle> keys;
    std::deque<TextureHandle> pending, ready;
    std::vector<Flight> flights;
    TextureHandle nextID = 1;
    size_t reserved = 0;
    bool stopped = false;
};
