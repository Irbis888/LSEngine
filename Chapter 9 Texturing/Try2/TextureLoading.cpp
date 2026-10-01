#include "TextureLoading.h"
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <stdexcept>
#include <wincodec.h>
#include <tracy/Tracy.hpp>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace {
void Check(HRESULT hr, const char* action) {
    if (FAILED(hr)) throw std::runtime_error(std::string(action) + " (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ")");
}
DXGI_FORMAT Linear(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_UNORM;
    case DXGI_FORMAT_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_UNORM;
    case DXGI_FORMAT_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_UNORM;
    case DXGI_FORMAT_BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_UNORM;
    default: return f;
    }
}
std::filesystem::path Resolve(const std::wstring& filename) {
    namespace fs = std::filesystem;
    fs::path path(filename);
    if (!path.is_absolute() && !fs::exists(path)) {
        const fs::path roots[] = { L"../../Textures", L"Textures" };
        for (const auto& root : roots) if (fs::exists(root / path)) { path = root / path; break; }
    }
    return fs::weakly_canonical(fs::absolute(path));
}
}
TextureLoader::~TextureLoader() { Shutdown(); }
TextureHandle TextureLoader::Request(const std::wstring& filename, TextureColorSpace color)
{
    if (stopped) throw std::logic_error("Texture loader is shut down");
    if (filename.empty()) return 0;
    const auto path = Resolve(filename);
    auto key = path.wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return std::towlower(c); });
    key += L"|" + std::to_wstring(static_cast<int>(color));
    if (auto it = keys.find(key); it != keys.end()) { Activate(it->second); return it->second; }
    if (textures.size() >= 65536) throw std::runtime_error("Texture registry limit exceeded");
    const auto id = nextID++;
    auto& tex = textures[id];
    tex.filename = path.wstring();
    const auto utf8 = path.u8string();
    tex.name.assign(utf8.begin(), utf8.end());
    tex.colorSpace = color;
    keys.emplace(std::move(key), id);
    pending.push_back(id);
    return id;
}
void TextureLoader::Activate(TextureHandle id)
{
    if (!id) return;
    auto& tex = Get(id);
    tex.active = true;
    if (tex.state == TextureState::Cancelled) { tex.state = TextureState::Queued; pending.push_back(id); }
}
void TextureLoader::Cancel(TextureHandle id)
{
    auto& tex = Get(id);
    if (tex.state == TextureState::Cancelled) return;
    for (auto& flight : flights) if (flight.id == id) flight.work->cancelled = true;
    ReleaseCPU(id);
    ++tex.generation;
    tex.active = false;
    tex.state = TextureState::Cancelled;
    tex.error.clear();
}
void TextureLoader::SetActive(const std::unordered_set<TextureHandle>& active)
{
    for (auto& [id, tex] : textures) {
        if (active.contains(id)) Activate(id);
        else Cancel(id);
    }
}
void TextureLoader::ReleaseCPU(TextureHandle id)
{
    auto& tex = Get(id);
    if (tex.pixels) { tex.pixels.reset(); --reserved; }
}
void TextureLoader::Fail(TextureHandle id, const std::string& error)
{
    auto& tex = Get(id);
    ReleaseCPU(id);
    tex.error = error;
    tex.state = TextureState::Failed;
}
void TextureLoader::Tick()
{
    ZoneScopedN("Texture CPU completion and dispatch");
    if (stopped) return;
    auto& jobs = JobSystem::Get();
    jobs.Init();
    for (auto it = flights.begin(); it != flights.end();) {
        if (!jobs.IsComplete(it->job)) { ++it; continue; }
        auto& tex = Get(it->id);
        const bool current = it->generation == tex.generation;
        std::string error;
        try { jobs.Wait(it->job); } catch (const std::exception& e) { error = e.what(); }
        catch (...) { error = "Unknown texture loading error"; }
        if (current && error.empty() && it->work->result) {
            tex.pixels = std::move(it->work->result);
            tex.state = TextureState::ReadyCPU;
            ready.push_back(it->id);
        } else {
            --reserved;
            if (current) Fail(it->id, error.empty() ? "Loading cancelled" : error);
        }
        it = flights.erase(it);
    }
    jobs.Collect();
    while (!pending.empty() && reserved < MaxResidentCPUJobs) {
        auto id = pending.front(); pending.pop_front();
        auto& tex = Get(id);
        if (tex.state != TextureState::Queued) continue;
        auto work = std::make_shared<Work>();
        auto job = jobs.Submit([work, path = tex.filename, color = tex.colorSpace] {
            ZoneScopedN("Texture read and decode");
            if (!work->cancelled) work->result = Read(path, color, work->cancelled);
        }, JobSystem::Queue::Loading);
        flights.push_back({ id, tex.generation, std::move(work), std::move(job) });
        ++reserved;
        tex.state = TextureState::LoadingCPU;
    }
    TracyPlot("Texture CPU reserved MiB", static_cast<int64_t>(reserved * 64));
}
TextureHandle TextureLoader::PopReady()
{
    while (!ready.empty()) {
        auto id = ready.front(); ready.pop_front();
        if (Get(id).state == TextureState::ReadyCPU) return id;
    }
    return 0;
}
void TextureLoader::Shutdown()
{
    if (stopped) return;
    stopped = true;
    for (auto& [id, tex] : textures) Cancel(id);
    for (auto& flight : flights) { try { JobSystem::Get().Wait(flight.job); } catch (...) {} }
    flights.clear(); pending.clear(); ready.clear(); reserved = 0;
}
TextureLoadingStats TextureLoader::Stats() const
{
    TextureLoadingStats s;
    s.reservedCPUBytes = reserved * MaxTextureBytes;
    for (const auto& [id, t] : textures) {
        switch (t.state) {
        case TextureState::Queued: ++s.queued; break;
        case TextureState::LoadingCPU: ++s.loading; break;
        case TextureState::ReadyCPU: ++s.readyCPU; break;
        case TextureState::UploadingGPU: ++s.uploading; break;
        case TextureState::Ready: ++s.ready; break;
        case TextureState::Failed: ++s.failed; break;
        case TextureState::Cancelled: ++s.cancelled; break;
        }
    }
    return s;
}
std::unique_ptr<TexturePixels> TextureLoader::Read(const std::wstring& filename,
    TextureColorSpace color, const std::atomic_bool& cancelled)
{
    auto result = std::make_unique<TexturePixels>();
    std::ifstream file(std::filesystem::path(filename), std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open texture");
    auto length = file.tellg();
    if (length <= 0 || static_cast<uint64_t>(length) > MaxTextureBytes) throw std::runtime_error("Texture file exceeds 64 MiB limit or is empty");
    result->bytes.resize(static_cast<size_t>(length));
    file.seekg(0);
    for (size_t pos = 0; pos < result->bytes.size();) {
        if (cancelled) return {};
        const auto count = std::min<size_t>(1024 * 1024, result->bytes.size() - pos);
        if (!file.read(reinterpret_cast<char*>(result->bytes.data() + pos), count)) throw std::runtime_error("Texture read failed");
        pos += count;
    }
    if (result->bytes.size() >= 4 && memcmp(result->bytes.data(), "DDS ", 4) == 0) {
        Check(DirectX::PrepareDDS12(result->bytes.data(), result->bytes.size(), color == TextureColorSpace::SRGB, result->image), "Invalid or unsupported 2D DDS");
        if (color == TextureColorSpace::Linear) result->image.desc.Format = Linear(result->image.desc.Format);
        return result;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Check(com, "Initialize image decoder");
    struct Uninit { ~Uninit() { CoUninitialize(); } } uninit;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "Create image decoder");
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream), "Create image stream");
    Check(stream->InitializeFromMemory(result->bytes.data(), static_cast<DWORD>(result->bytes.size())), "Open image bytes");
    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder), "Decode image");
    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0, &frame), "Read image frame");
    UINT width = 0, height = 0;
    Check(frame->GetSize(&width, &height), "Read image size");
    const uint64_t size = uint64_t(width) * height * 4;
    if (!width || !height || width > 16384 || height > 16384 || size + result->bytes.size() > MaxTextureBytes)
        throw std::runtime_error("Decoded image exceeds 64 MiB or dimension limit");
    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(&converter), "Create RGBA converter");
    Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert image to RGBA");
    std::vector<uint8_t> pixels(static_cast<size_t>(size));
    if (cancelled) return {};
    Check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(size), pixels.data()), "Decode RGBA pixels");
    // Release the decoder before releasing its source bytes.
    converter.Reset(); frame.Reset(); decoder.Reset(); stream.Reset();
    result->bytes = std::move(pixels);
    result->image.desc = CD3DX12_RESOURCE_DESC::Tex2D(color == TextureColorSpace::SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
    result->image.subresources.push_back({ result->bytes.data(), static_cast<LONG_PTR>(width * 4), static_cast<LONG_PTR>(size) });
    return result;
}
