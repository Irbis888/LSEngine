#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ImageIO.h"
#include "DDSTextureLoader.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace
{
    std::string PathText(const fs::path& path)
    {
        const auto text = path.u8string();
        return { reinterpret_cast<const char*>(text.data()), text.size() };
    }

    std::string HResultText(HRESULT hr)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned>(hr));
        return buffer;
    }

    bool IsSrgb(DXGI_FORMAT format)
    {
        return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    }

    // Channel count of 8-bit-per-channel formats handled by mip generation and encoders; 0 otherwise.
    uint32_t Channels8(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return 4;
        case DXGI_FORMAT_R8_UNORM:
            return 1;
        default:
            return 0;
        }
    }

    bool IsBgr(DXGI_FORMAT format)
    {
        return format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            format == DXGI_FORMAT_B8G8R8X8_UNORM || format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    }

    bool HasNoAlpha(DXGI_FORMAT format)
    {
        return format == DXGI_FORMAT_B8G8R8X8_UNORM || format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    }

    // Validates a writable image and returns its top-level subresource.
    const ImageSubresource& TopLevel(const ImageData& image, const char* encoder)
    {
        const auto fail = [&](const char* reason)
        { throw std::invalid_argument(std::string(encoder) + " export: " + reason); };
        if (image.dimension != ImageDimension::Texture2D || image.arraySize != 1 || image.isCubeMap || image.depth != 1)
            fail("only a single 2D image can be written (no arrays, cube maps or volumes)");
        if (image.width == 0 || image.height == 0 || image.subresources.empty())
            fail("image is empty");
        const auto& top = image.subresources[0];
        const size_t bytesPerPixel = image.format == DXGI_FORMAT_R16_UNORM ? 2 : Channels8(image.format);
        if (bytesPerPixel == 0)
            fail("unsupported pixel format (use RGBA8/BGRA8/BGRX8, R8_UNORM or R16_UNORM; compressed DDS must be decompressed first)");
        if (top.rowPitch < size_t(image.width) * bytesPerPixel ||
            top.offset + top.rowPitch * (image.height - 1) + size_t(image.width) * bytesPerPixel > image.pixels.size())
            fail("pixel data is smaller than the declared size");
        return top;
    }

    // Top mip as tightly packed BGRA8 (for TGA and WIC), forcing alpha to 255 for X8 formats.
    std::vector<uint8_t> PackBgra(const ImageData& image, const ImageSubresource& top)
    {
        std::vector<uint8_t> result(size_t(image.width) * image.height * 4);
        const bool bgr = IsBgr(image.format), opaque = HasNoAlpha(image.format);
        for (uint32_t y = 0; y < image.height; ++y)
        {
            const uint8_t* src = image.pixels.data() + top.offset + top.rowPitch * y;
            uint8_t* dst = result.data() + size_t(y) * image.width * 4;
            for (uint32_t x = 0; x < image.width; ++x, src += 4, dst += 4)
            {
                dst[0] = bgr ? src[0] : src[2];
                dst[1] = src[1];
                dst[2] = bgr ? src[2] : src[0];
                dst[3] = opaque ? 255 : src[3];
            }
        }
        return result;
    }

    std::vector<uint8_t> PackRows(const ImageData& image, const ImageSubresource& top, size_t bytesPerPixel)
    {
        const size_t row = size_t(image.width) * bytesPerPixel;
        std::vector<uint8_t> result(row * image.height);
        for (uint32_t y = 0; y < image.height; ++y)
            std::memcpy(result.data() + row * y, image.pixels.data() + top.offset + top.rowPitch * y, row);
        return result;
    }

    void WriteFile(const fs::path& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot open image file for writing: " + PathText(path));
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        file.close();
        if (!file) throw std::runtime_error("Failed to write image file: " + PathText(path));
    }

    // Initializes COM for WIC on the calling thread when needed. A thread that
    // already uses a different apartment model can still use the WIC factory.
    struct ComScope
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
    };

    void CheckWic(HRESULT hr, const char* step)
    {
        if (FAILED(hr)) throw std::runtime_error(std::string("PNG export: ") + step + " failed (HRESULT " + HResultText(hr) + ")");
    }

    float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
    float LinearToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }
}

ImageIO::FileFormat ImageIO::FormatFromPath(const fs::path& path)
{
    const auto extension = path.extension().wstring();
    if (_wcsicmp(extension.c_str(), L".dds") == 0) return FileFormat::Dds;
    if (_wcsicmp(extension.c_str(), L".png") == 0) return FileFormat::Png;
    if (_wcsicmp(extension.c_str(), L".tga") == 0) return FileFormat::Tga;
    return FileFormat::Unknown;
}

ImageData ImageIO::FromRgba8(uint32_t width, uint32_t height, std::vector<uint8_t> pixels)
{
    if (width == 0 || height == 0 || pixels.size() != size_t(width) * height * 4)
        throw std::invalid_argument("RGBA8 pixel buffer does not match width * height * 4");
    ImageData image;
    image.width = width;
    image.height = height;
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image.pixels = std::move(pixels);
    image.subresources.push_back({ 0, size_t(width) * 4, size_t(width) * height * 4 });
    return image;
}

ImageData ImageIO::Load(const fs::path& path, const LoadOptions& options)
{
    ZoneScopedN("Texture CPU read and decode");
    const FileFormat format = FormatFromPath(path);
    if (format == FileFormat::Dds)
    {
        ImageData image;
        const HRESULT hr = DirectX::LoadDDSImageFromFile(path.c_str(), image);
        if (FAILED(hr))
            throw std::runtime_error("Failed to load DDS texture: " + PathText(path) +
                " (HRESULT " + std::to_string(static_cast<uint32_t>(hr)) + ")");
        return image;
    }

    FILE* handle = nullptr;
    if (_wfopen_s(&handle, path.c_str(), L"rb") != 0 || !handle)
        throw std::runtime_error("Failed to open texture: " + PathText(path));
    std::unique_ptr<FILE, decltype(&std::fclose)> file(handle, &std::fclose);
    int width = 0, height = 0, channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_file(file.get(), &width, &height, &channels, STBI_rgb_alpha), &stbi_image_free);
    if (!pixels)
    {
        const char* kind = format == FileFormat::Png ? "PNG " : format == FileFormat::Tga ? "TGA " : "";
        throw std::runtime_error(std::string("Failed to decode ") + kind + "texture: " + PathText(path) +
            " (" + stbi_failure_reason() + ")");
    }
    const size_t bytes = size_t(width) * size_t(height) * 4;
    ImageData image = FromRgba8(uint32_t(width), uint32_t(height),
        std::vector<uint8_t>(pixels.get(), pixels.get() + bytes));
    pixels.reset();
    if (options.generateMips) GenerateMips(image);
    return image;
}

void ImageIO::GenerateMips(ImageData& image)
{
    ZoneScopedN("Texture CPU mip generation");
    const uint32_t channels = Channels8(image.format);
    if (channels == 0 || image.dimension != ImageDimension::Texture2D || image.arraySize != 1 ||
        image.isCubeMap || image.depth != 1 || image.width == 0 || image.height == 0 || image.subresources.empty())
        throw std::invalid_argument("Mip generation supports a single 2D image with 8-bit RGBA/BGRA/R8 pixels");

    uint32_t levels = 1;
    for (uint32_t size = std::max(image.width, image.height); size > 1; size >>= 1) ++levels;

    // Pack mip 0 tightly, then append every following level.
    const ImageSubresource top = image.subresources[0];
    const size_t topRow = size_t(image.width) * channels;
    std::vector<uint8_t> pixels;
    std::vector<ImageSubresource> subresources;
    {
        size_t total = 0;
        for (uint32_t level = 0, w = image.width, h = image.height; level < levels; ++level)
        {
            total += size_t(w) * h * channels;
            w = std::max(1u, w / 2); h = std::max(1u, h / 2);
        }
        pixels.resize(total);
    }
    for (uint32_t y = 0; y < image.height; ++y)
        std::memcpy(pixels.data() + topRow * y, image.pixels.data() + top.offset + top.rowPitch * y, topRow);
    subresources.push_back({ 0, topRow, topRow * image.height });

    std::array<float, 256> toLinear{};
    const bool srgb = IsSrgb(image.format);
    for (int i = 0; i < 256; ++i) toLinear[i] = srgb ? SrgbToLinear(i / 255.0f) : i / 255.0f;
    const auto encode = [&](float value, bool color)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        if (srgb && color) value = LinearToSrgb(value);
        return static_cast<uint8_t>(std::lround(value * 255.0f));
    };

    uint32_t srcW = image.width, srcH = image.height;
    for (uint32_t level = 1; level < levels; ++level)
    {
        const uint32_t dstW = std::max(1u, srcW / 2), dstH = std::max(1u, srcH / 2);
        const ImageSubresource& src = subresources.back();
        const size_t dstOffset = src.offset + src.slicePitch;
        const size_t dstRow = size_t(dstW) * channels;
        // Box filter: every source pixel contributes to exactly one destination
        // pixel, so odd sizes average 2 or 3 source pixels per axis.
        for (uint32_t y = 0; y < dstH; ++y)
        {
            const uint32_t y0 = uint32_t(uint64_t(y) * srcH / dstH);
            const uint32_t y1 = std::max(y0 + 1, uint32_t(uint64_t(y + 1) * srcH / dstH));
            for (uint32_t x = 0; x < dstW; ++x)
            {
                const uint32_t x0 = uint32_t(uint64_t(x) * srcW / dstW);
                const uint32_t x1 = std::max(x0 + 1, uint32_t(uint64_t(x + 1) * srcW / dstW));
                float sum[4] = {};
                for (uint32_t sy = y0; sy < y1; ++sy)
                {
                    const uint8_t* row = pixels.data() + src.offset + src.rowPitch * sy;
                    for (uint32_t sx = x0; sx < x1; ++sx)
                        for (uint32_t c = 0; c < channels; ++c)
                        {
                            const uint8_t v = row[sx * channels + c];
                            sum[c] += (channels == 4 && c == 3) ? v / 255.0f : toLinear[v];
                        }
                }
                const float scale = 1.0f / float((x1 - x0) * (y1 - y0));
                uint8_t* out = pixels.data() + dstOffset + dstRow * y + size_t(x) * channels;
                for (uint32_t c = 0; c < channels; ++c)
                    out[c] = encode(sum[c] * scale, !(channels == 4 && c == 3));
            }
        }
        subresources.push_back({ dstOffset, dstRow, dstRow * dstH });
        srcW = dstW; srcH = dstH;
    }
    image.pixels = std::move(pixels);
    image.subresources = std::move(subresources);
    image.mipLevels = levels;
}

std::vector<uint8_t> ImageIO::EncodeTga(const ImageData& image, bool rle)
{
    const ImageSubresource& top = TopLevel(image, "TGA");
    if (image.format == DXGI_FORMAT_R16_UNORM)
        throw std::invalid_argument("TGA export: 16-bit grayscale is not representable; save R16_UNORM as PNG");
    if (image.width > 0xFFFF || image.height > 0xFFFF)
        throw std::invalid_argument("TGA export: width and height must not exceed 65535");

    const bool gray = image.format == DXGI_FORMAT_R8_UNORM;
    const size_t bpp = gray ? 1 : 4;
    const std::vector<uint8_t> data = gray ? PackRows(image, top, 1) : PackBgra(image, top);

    std::vector<uint8_t> out(18, 0);
    out[2] = uint8_t((gray ? 3 : 2) + (rle ? 8 : 0));            // (RLE) grayscale / true-color
    out[12] = uint8_t(image.width & 0xFF);  out[13] = uint8_t(image.width >> 8);
    out[14] = uint8_t(image.height & 0xFF); out[15] = uint8_t(image.height >> 8);
    out[16] = uint8_t(bpp * 8);
    out[17] = uint8_t(0x20 | (gray ? 0 : 8));                     // top-left origin, alpha bits
    out.reserve(out.size() + data.size() + 26);

    const size_t row = size_t(image.width) * bpp;
    for (uint32_t y = 0; y < image.height; ++y)
    {
        const uint8_t* line = data.data() + row * y;
        if (!rle) { out.insert(out.end(), line, line + row); continue; }
        // Packets never cross scanlines (TGA 2.0 recommendation).
        const auto same = [&](uint32_t a, uint32_t b) { return std::memcmp(line + a * bpp, line + b * bpp, bpp) == 0; };
        for (uint32_t x = 0; x < image.width;)
        {
            uint32_t run = 1;
            while (x + run < image.width && run < 128 && same(x, x + run)) ++run;
            if (run >= 2)
            {
                out.push_back(uint8_t(0x80 | (run - 1)));
                out.insert(out.end(), line + x * bpp, line + (x + 1) * bpp);
                x += run;
                continue;
            }
            const uint32_t start = x;
            uint32_t count = 0;
            while (x < image.width && count < 128 && !(x + 1 < image.width && same(x, x + 1))) { ++x; ++count; }
            if (count == 0) { ++x; count = 1; } // defensive; a run would have been taken above
            out.push_back(uint8_t(count - 1));
            out.insert(out.end(), line + start * bpp, line + (start + count) * bpp);
        }
    }
    // TGA 2.0 footer: no extension or developer area.
    static const char signature[] = "TRUEVISION-XFILE.";
    out.insert(out.end(), 8, 0);
    out.insert(out.end(), signature, signature + sizeof(signature)); // includes the trailing '\0'
    return out;
}

std::vector<uint8_t> ImageIO::EncodePng(const ImageData& image)
{
    ZoneScopedN("PNG encode");
    const ImageSubresource& top = TopLevel(image, "PNG");
    WICPixelFormatGUID wicFormat;
    std::vector<uint8_t> data;
    UINT bytesPerPixel;
    if (image.format == DXGI_FORMAT_R16_UNORM)
    { wicFormat = GUID_WICPixelFormat16bppGray; data = PackRows(image, top, 2); bytesPerPixel = 2; }
    else if (image.format == DXGI_FORMAT_R8_UNORM)
    { wicFormat = GUID_WICPixelFormat8bppGray; data = PackRows(image, top, 1); bytesPerPixel = 1; }
    else
    { wicFormat = GUID_WICPixelFormat32bppBGRA; data = PackBgra(image, top); bytesPerPixel = 4; }

    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    CheckWic(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "creating WIC factory");
    ComPtr<IStream> stream;
    CheckWic(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "creating memory stream");
    ComPtr<IWICBitmapEncoder> encoder;
    CheckWic(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "creating encoder");
    CheckWic(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "initializing encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    CheckWic(encoder->CreateNewFrame(&frame, &properties), "creating frame");
    CheckWic(frame->Initialize(properties.Get()), "initializing frame");
    CheckWic(frame->SetSize(image.width, image.height), "setting size");
    WICPixelFormatGUID accepted = wicFormat;
    CheckWic(frame->SetPixelFormat(&accepted), "setting pixel format");
    if (!IsEqualGUID(accepted, wicFormat))
        throw std::runtime_error("PNG export: WIC encoder does not accept the requested pixel format");
    const UINT stride = image.width * bytesPerPixel;
    CheckWic(frame->WritePixels(image.height, stride, UINT(data.size()), data.data()), "writing pixels");
    CheckWic(frame->Commit(), "committing frame");
    CheckWic(encoder->Commit(), "committing file");

    STATSTG stat{};
    CheckWic(stream->Stat(&stat, STATFLAG_NONAME), "reading stream size");
    HGLOBAL memory = nullptr;
    CheckWic(GetHGlobalFromStream(stream.Get(), &memory), "accessing stream memory");
    const auto* bytes = static_cast<const uint8_t*>(GlobalLock(memory));
    if (!bytes) throw std::runtime_error("PNG export: cannot lock encoded data");
    std::vector<uint8_t> out(bytes, bytes + size_t(stat.cbSize.QuadPart));
    GlobalUnlock(memory);
    return out;
}

void ImageIO::SavePng(const fs::path& path, const ImageData& image) { WriteFile(path, EncodePng(image)); }
void ImageIO::SaveTga(const fs::path& path, const ImageData& image, bool rle) { WriteFile(path, EncodeTga(image, rle)); }

void ImageIO::Save(const fs::path& path, const ImageData& image)
{
    switch (FormatFromPath(path))
    {
    case FileFormat::Png: SavePng(path, image); return;
    case FileFormat::Tga: SaveTga(path, image); return;
    default: throw std::invalid_argument("Cannot save " + PathText(path) + ": use a .png or .tga extension");
    }
}
