#include "ImageIO.h"
#include "ResourceManager.h"
#include "ThirdParty/stb/stb_image.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace fs = std::filesystem;

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    template <class F> bool Throws(F&& action)
    {
        try { action(); } catch (const std::exception&) { return true; }
        return false;
    }

    std::vector<uint8_t> Top(const ImageData& image)
    {
        const auto& top = image.subresources.at(0);
        return { image.pixels.begin() + top.offset, image.pixels.begin() + top.offset + top.slicePitch };
    }

    ImageData Pattern(uint32_t width, uint32_t height)
    {
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint8_t(i * 37 + i / 7);
        pixels[3] = 0; // fully transparent pixel survives both encoders
        return ImageIO::FromRgba8(width, height, std::move(pixels));
    }

    ImageData Gray(uint32_t width, uint32_t height, DXGI_FORMAT format, size_t bytesPerPixel)
    {
        ImageData image;
        image.width = width; image.height = height; image.format = format;
        // Padded rows: encoders must honour rowPitch rather than assume tight packing.
        const size_t rowPitch = width * bytesPerPixel + 3;
        image.pixels.resize(rowPitch * height);
        for (size_t i = 0; i < image.pixels.size(); ++i) image.pixels[i] = uint8_t(i * 11);
        image.subresources.push_back({ 0, rowPitch, rowPitch * height });
        return image;
    }
}

void RunImageIOTests(const fs::path& fixtures)
{
    using namespace ImageIO;
    Check(FormatFromPath("a/B.PNG") == FileFormat::Png && FormatFromPath("x.Tga") == FileFormat::Tga &&
        FormatFromPath("x.dds") == FileFormat::Dds && FormatFromPath("x.jpg") == FileFormat::Unknown,
        "Extension detection failed");

    // RGBA round trips through PNG, RLE TGA and raw TGA, with Unicode names and an odd size.
    const ImageData rgba = Pattern(5, 3);
    const LoadOptions noMips{ false };
    for (const auto* name : { L"картинка.png", L"rle.TGA", L"raw.tga" })
    {
        const fs::path path = fixtures / name;
        if (std::wstring(name) == L"raw.tga") SaveTga(path, rgba, false); else Save(path, rgba);
        const ImageData loaded = Load(path, noMips);
        Check(loaded.width == 5 && loaded.height == 3 && loaded.mipLevels == 1 && loaded.pixels == rgba.pixels,
            "PNG/TGA round trip changed pixels");
        fs::remove(path);
    }

    // BGRA input is swizzled; BGRX alpha is written as opaque.
    ImageData bgra = rgba;
    bgra.format = DXGI_FORMAT_B8G8R8X8_UNORM;
    for (const auto* name : { L"bgrx.png", L"bgrx.tga" })
    {
        Save(fixtures / name, bgra);
        const ImageData loaded = Load(fixtures / name, noMips);
        for (size_t i = 0; i < loaded.pixels.size(); i += 4)
            Check(loaded.pixels[i] == rgba.pixels[i + 2] && loaded.pixels[i + 1] == rgba.pixels[i + 1] &&
                loaded.pixels[i + 2] == rgba.pixels[i] && loaded.pixels[i + 3] == 255, "BGRX export mismatch");
        fs::remove(fixtures / name);
    }

    // 8-bit grayscale (PNG and TGA) and 16-bit grayscale PNG with padded rows.
    const ImageData gray8 = Gray(7, 2, DXGI_FORMAT_R8_UNORM, 1);
    for (const auto* name : { L"gray.png", L"gray.tga" })
    {
        Save(fixtures / name, gray8);
        const ImageData loaded = Load(fixtures / name, noMips);
        for (uint32_t y = 0; y < 2; ++y) for (uint32_t x = 0; x < 7; ++x)
        {
            const uint8_t expected = gray8.pixels[gray8.subresources[0].rowPitch * y + x];
            const uint8_t* p = loaded.pixels.data() + (y * 7 + x) * 4;
            Check(p[0] == expected && p[1] == expected && p[2] == expected && p[3] == 255, "Grayscale export mismatch");
        }
        fs::remove(fixtures / name);
    }
    const ImageData gray16 = Gray(4, 3, DXGI_FORMAT_R16_UNORM, 2);
    const auto png16 = EncodePng(gray16);
    int w = 0, h = 0, c = 0;
    Check(stbi_is_16_bit_from_memory(png16.data(), int(png16.size())) != 0, "R16 PNG is not 16-bit");
    std::unique_ptr<stbi_us, decltype(&stbi_image_free)> samples(
        stbi_load_16_from_memory(png16.data(), int(png16.size()), &w, &h, &c, 1), &stbi_image_free);
    Check(samples && w == 4 && h == 3 && c == 1, "R16 PNG did not decode as grayscale");
    for (uint32_t y = 0; y < 3; ++y) for (uint32_t x = 0; x < 4; ++x)
    {
        uint16_t expected;
        std::memcpy(&expected, gray16.pixels.data() + gray16.subresources[0].rowPitch * y + x * 2, 2);
        Check(samples.get()[y * 4 + x] == expected, "R16 PNG sample mismatch");
    }

    // RLE actually compresses flat images; raw TGA is header + pixels + footer.
    const ImageData flat = ImageIO::FromRgba8(64, 64, std::vector<uint8_t>(64 * 64 * 4, 200));
    Check(EncodeTga(flat, true).size() < 2048 && EncodeTga(flat, false).size() == 18 + 64 * 64 * 4 + 26,
        "TGA RLE/raw sizes are wrong");

    // Mip chain: odd sizes, offsets, and box-filter values.
    ImageData mips = Pattern(5, 3);
    GenerateMips(mips);
    Check(mips.mipLevels == 3 && mips.subresources.size() == 3 && mips.pixels.size() == (15 + 2 + 1) * 4 &&
        mips.subresources[1].offset == 60 && mips.subresources[1].rowPitch == 8 &&
        mips.subresources[2].offset == 68 && mips.subresources[2].slicePitch == 4, "Mip layout is wrong");
    Check(Top(mips) == rgba.pixels, "Mip generation changed mip 0");
    ImageData uniform = ImageIO::FromRgba8(9, 4, std::vector<uint8_t>(9 * 4 * 4, 77));
    GenerateMips(uniform);
    Check(uniform.mipLevels == 4, "9x4 needs 4 mips");
    for (uint8_t v : uniform.pixels) Check(v == 77, "Box filter changed a uniform image");
    ImageData srgb = ImageIO::FromRgba8(2, 1, { 0, 0, 0, 0, 255, 255, 255, 255 });
    srgb.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    GenerateMips(srgb);
    Check(srgb.pixels[8] == 188 && srgb.pixels[11] == 128, "sRGB mips must be averaged in linear space");

    // Default load builds mips for PNG/TGA.
    Save(fixtures / "mips.png", rgba);
    const ImageData withMips = Load(fixtures / "mips.png");
    Check(withMips.mipLevels == 3 && Top(withMips) == rgba.pixels, "PNG load did not generate mips");
    fs::remove(fixtures / "mips.png");

    // Unsupported exports are rejected with an exception, not a broken file.
    ImageData bc1 = rgba;
    bc1.format = DXGI_FORMAT_BC1_UNORM;
    ImageData cube = rgba;
    cube.isCubeMap = true; cube.arraySize = 6;
    Check(Throws([&] { Save(fixtures / "x.jpg", rgba); }) && Throws([&] { Save(fixtures / "x.png", bc1); }) &&
        Throws([&] { Save(fixtures / "x.png", cube); }) && Throws([&] { Save(fixtures / "x.tga", gray16); }),
        "Invalid exports were accepted");
    Check(!fs::exists(fixtures / "x.png") && !fs::exists(fixtures / "x.tga"), "Rejected export left a file");
    Check(Throws([&] { Load(fixtures / "missing.png"); }), "Missing file was accepted");

    // ResourceManager entry points.
    ResourceManager resources;
    const fs::path source = fixtures / "source.tga";
    ResourceManager::SaveImage(source, rgba);
    const TextureID id = resources.LoadTexture(source.wstring());
    const fs::path copy = fixtures / L"копия.png";
    resources.SaveTexture(id, copy);
    Check(Load(copy, noMips).pixels == rgba.pixels, "SaveTexture round trip failed");
    fs::remove(source);
    fs::remove(copy);
}
