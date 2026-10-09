#include "ResourceManager.h"
#include "D3DRenderAdapter.h"
#include <d3d12sdklayers.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
void RunResourceStreamingTests(const fs::path& repo, const fs::path& fixtures);
void RunSceneSwitchStreamingTests(const fs::path& repo, const fs::path& fixtures);

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void Write(const fs::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Check(file.good(), "Could not write texture fixture");
}

static std::vector<uint8_t> DDS(uint32_t dimension = 3, uint32_t arraySize = 1,
    uint32_t cube = 0, uint32_t depth = 1, bool compressed = true)
{
    std::vector<uint8_t> bytes(148);
    const auto put = [&](size_t offset, uint32_t value) { std::memcpy(bytes.data() + offset, &value, 4); };
    put(0, 0x20534444); put(4, 124); put(8, 0x1007 | (dimension == 4 ? 0x800000 : 0));
    put(12, dimension == 2 ? 1 : 4); put(16, 4); put(24, depth); put(28, 3);
    put(76, 32); put(80, 4); put(84, 0x30315844); put(108, 0x1000);
    put(128, compressed ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM);
    put(132, dimension); put(136, cube ? 4 : 0); put(140, arraySize);
    const uint32_t slices = arraySize * (cube ? 6 : 1);
    for (uint32_t slice = 0; slice < slices; ++slice)
    {
        uint32_t w = 4, h = dimension == 2 ? 1 : 4, d = depth;
        for (uint32_t mip = 0; mip < 3; ++mip)
        {
            const size_t size = compressed ? 8 : static_cast<size_t>(w) * h * 4 * d;
            for (size_t i = 0; i < size; ++i) bytes.push_back(static_cast<uint8_t>(i + mip + slice));
            w = std::max(1u, w / 2); h = std::max(1u, h / 2); d = std::max(1u, d / 2);
        }
    }
    return bytes;
}

struct Readback
{
    TextureID id;
    ComPtr<ID3D12Resource> buffer;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
    std::vector<UINT> rows;
    std::vector<UINT64> rowSizes;
};

static void VerifyUploads(ResourceManager& resources, const std::vector<TextureID>& ids)
{
    const HINSTANCE instance = GetModuleHandle(nullptr);
    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc; wc.hInstance = instance; wc.lpszClassName = L"TextureLoadingTest";
    Check(RegisterClass(&wc) != 0, "Could not register test window");
    HWND window = CreateWindow(wc.lpszClassName, L"Texture test", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, instance, nullptr);
    Check(window != nullptr, "Could not create hidden test window");
    {
        D3DRenderAdapter renderer;
        renderer.SetResourceManager(&resources);
        renderer.Init(window, 64, 64);
        const auto bindings = renderer.GetImGuiBindings();
        ComPtr<ID3D12InfoQueue> diagnostics;
        bindings.Device->QueryInterface(IID_PPV_ARGS(&diagnostics));
        if (diagnostics) diagnostics->ClearStoredMessages();
        renderer.BeginFrame();
        std::vector<Readback> readbacks;
        for (TextureID id : ids)
        {
            const int srv = renderer.UploadTexture(id);
            Check(srv >= 0 && renderer.UploadTexture(id) == srv, "GPU upload/cache failed");
            auto& gpu = *renderer.mTextures.at(std::to_string(id));
            const auto desc = gpu.Resource->GetDesc();
            const auto& image = resources.GetTexture(id).imageData;
            Check(desc.Format == image.format && desc.MipLevels == image.mipLevels, "GPU metadata differs");
            const UINT count = static_cast<UINT>(image.subresources.size());
            Readback readback{ id };
            readback.footprints.resize(count); readback.rows.resize(count); readback.rowSizes.resize(count);
            UINT64 size = 0;
            bindings.Device->GetCopyableFootprints(&desc, 0, count, 0, readback.footprints.data(),
                readback.rows.data(), readback.rowSizes.data(), &size);
            const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_READBACK);
            const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
            ThrowIfFailed(bindings.Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback.buffer)));
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(gpu.Resource.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
            bindings.CommandList->ResourceBarrier(1, &barrier);
            for (UINT i = 0; i < count; ++i)
            {
                const CD3DX12_TEXTURE_COPY_LOCATION source(gpu.Resource.Get(), i);
                const CD3DX12_TEXTURE_COPY_LOCATION target(readback.buffer.Get(), readback.footprints[i]);
                bindings.CommandList->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
            }
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(gpu.Resource.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            bindings.CommandList->ResourceBarrier(1, &barrier);
            readbacks.push_back(std::move(readback));
        }
        Material textured;
        textured.name = "Texture test material";
        textured.albedo = ids[0];
        textured.normal = ids[1];
        auto* material = renderer.GetOrLoadMaterial(resources.CreateMaterial(textured));
        Check(material && material->DiffuseSrvHeapIndex == renderer.UploadTexture(ids[0]) &&
            material->NormalSrvHeapIndex == renderer.UploadTexture(ids[1]), "Material did not use CPU textures");
        auto* fallback = renderer.GetOrLoadMaterial(resources.CreateSolidMaterial("Fallback", glm::vec3(1.0f)));
        Check(fallback && fallback->DiffuseSrvHeapIndex >= 0 && fallback->NormalSrvHeapIndex >= 0,
            "Default textures did not use ResourceManager");
        renderer.EndFrame();
        renderer.FlushCommandQueue();
        for (auto& readback : readbacks)
        {
            const auto& image = resources.GetTexture(readback.id).imageData;
            uint8_t* mapped = nullptr;
            ThrowIfFailed(readback.buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
            for (size_t i = 0; i < image.subresources.size(); ++i)
            {
                const auto& footprint = readback.footprints[i];
                const auto& source = image.subresources[i];
                for (UINT z = 0; z < footprint.Footprint.Depth; ++z)
                    for (UINT row = 0; row < readback.rows[i]; ++row)
                        Check(std::memcmp(image.pixels.data() + source.offset + z * source.slicePitch + row * source.rowPitch,
                            mapped + footprint.Offset + (z * readback.rows[i] + row) * footprint.Footprint.RowPitch,
                            static_cast<size_t>(readback.rowSizes[i])) == 0, "GPU texture bytes differ from ImageData");
            }
            const D3D12_RANGE noWrite = { 0, 0 };
            readback.buffer->Unmap(0, &noWrite);
        }
        if (diagnostics)
            for (UINT64 i = 0; i < diagnostics->GetNumStoredMessages(); ++i)
            {
                SIZE_T size = 0; diagnostics->GetMessage(i, nullptr, &size);
                std::vector<uint8_t> bytes(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
                diagnostics->GetMessage(i, message, &size);
                Check(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                    message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION, message->pDescription);
            }
    }
    DestroyWindow(window);
    UnregisterClass(wc.lpszClassName, instance);
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 3, "Expected repository and fixture directory paths");
        const fs::path repo(argv[1]), fixtures(argv[2]);
        fs::create_directories(fixtures);
        ResourceManager resources;
        // Top-left origin, two BGRA pixels with alpha, Unicode filename.
        const fs::path tga = fixtures / L"\u0442\u0435\u043a\u0441\u0442\u0443\u0440\u0430.TGA";
        std::vector<uint8_t> tgaBytes(18);
        tgaBytes[2] = 2; tgaBytes[12] = 2; tgaBytes[14] = 1; tgaBytes[16] = 32; tgaBytes[17] = 0x28;
        tgaBytes.insert(tgaBytes.end(), { 0, 0, 255, 128, 0, 255, 0, 255 });
        Write(tga, tgaBytes);
        const TextureID tgaID = resources.LoadTexture(tga.wstring());
        const ImageData& image = resources.GetTexture(tgaID).imageData;
        Check(image.width == 2 && image.height == 1 && image.format == DXGI_FORMAT_R8G8B8A8_UNORM &&
            image.pixels == std::vector<uint8_t>({ 255, 0, 0, 128, 0, 255, 0, 255 }), "stb RGBA decode failed");
        Check(resources.LoadTexture((fixtures / L"." / tga.filename()).wstring()) == tgaID, "CPU cache did not reuse TextureID");
        std::vector<TextureID> ids{ tgaID };
        for (const auto& fixture : { std::pair{ "mips.DDS", DDS() },
            std::pair{ "array.dds", DDS(3, 2) }, std::pair{ "cube.dds", DDS(3, 2, 1) },
            std::pair{ "1d.dds", DDS(2, 2, 0, 1, false) }, std::pair{ "volume.dds", DDS(4, 1, 0, 4, false) } })
        {
            const fs::path path = fixtures / fixture.first;
            Write(path, fixture.second);
            ids.push_back(resources.LoadTexture(path.wstring()));
            fs::remove(path); // GPU must consume ImageData after the source file is gone.
        }
        const auto& dds = resources.GetTexture(ids[1]).imageData;
        Check(dds.format == DXGI_FORMAT_BC1_UNORM && dds.mipLevels == 3 && dds.pixels.size() == 24 &&
            dds.subresources[2].offset == 16 && dds.subresources[0].rowPitch == 8, "DDS mip/block layout lost");
        auto corrupt = DDS(); corrupt.pop_back();
        const auto broken = fixtures / "broken.dds";
        Write(broken, corrupt);
        bool rejected = false;
        try { resources.LoadTexture(broken.wstring()); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected, "Truncated DDS accepted");
        Write(broken, DDS());
        ids.push_back(resources.LoadTexture(broken.wstring()));
        fs::remove(broken);
        fs::remove(tga);
        ids.push_back(resources.LoadTexture((repo / "Textures/textures/Head_Texture.png").wstring()));
        ids.push_back(resources.LoadTexture((repo / "Textures/white1x1.dds").wstring()));
        VerifyUploads(resources, ids);
        RunResourceStreamingTests(repo, fixtures);
        RunSceneSwitchStreamingTests(repo, fixtures);
        std::cout << "PASS: stb RGBA/PNG/Unicode; DDS mips/arrays/cubes/1D/3D; cache; invalid-file retry; GPU readback without source files\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; }
    catch (const DxException& error) { std::wcerr << L"FAIL: " << error.ToString() << L'\n'; }
    return 1;
}
