#pragma once

#include "ImageData.h"
#include <cstdint>
#include <filesystem>
#include <vector>

// CPU image file I/O shared by ResourceManager, Terrain and tools.
//
// Reading:  DDS (DDSTextureLoader, keeps compression/mips/arrays),
//           PNG and TGA (stb_image -> RGBA8, optional generated mip chain),
//           other stb formats (BMP, JPG, ...) the same way as PNG/TGA.
// Writing:  PNG (Windows Imaging Component) and TGA (built-in, optional RLE).
//           Only the top mip of a single 2D image is written; supported formats:
//           R8G8B8A8, B8G8R8A8, B8G8R8X8 (+_SRGB), R8_UNORM, and R16_UNORM (PNG only).
namespace ImageIO
{
    enum class FileFormat { Unknown, Dds, Png, Tga };

    // Case-insensitive extension check (.dds/.png/.tga); Unknown for anything else.
    FileFormat FormatFromPath(const std::filesystem::path& path);

    struct LoadOptions
    {
        // Build a full box-filtered mip chain for decoded (non-DDS) images.
        // DDS files keep exactly the mips stored in the file.
        bool generateMips = true;
    };

    // Throws std::runtime_error with the file name and reason on failure.
    ImageData Load(const std::filesystem::path& path, const LoadOptions& options = {});

    // Replaces the mip chain of a single 2D image with a full chain built from mip 0.
    // Supports 8-bit UNORM/SRGB formats with 1 or 4 channels; SRGB is filtered in linear space.
    // Throws std::invalid_argument for unsupported images.
    void GenerateMips(ImageData& image);

    // Encode the top mip into an in-memory file.
    std::vector<uint8_t> EncodePng(const ImageData& image);
    std::vector<uint8_t> EncodeTga(const ImageData& image, bool rle = true);

    // Write by explicit format, or pick PNG/TGA from the extension (throws for others).
    void SavePng(const std::filesystem::path& path, const ImageData& image);
    void SaveTga(const std::filesystem::path& path, const ImageData& image, bool rle = true);
    void Save(const std::filesystem::path& path, const ImageData& image);

    // Wraps tightly packed RGBA8 pixels (width * height * 4 bytes) as a one-mip ImageData.
    ImageData FromRgba8(uint32_t width, uint32_t height, std::vector<uint8_t> pixels);
}
