#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <dxgiformat.h>

enum class ImageDimension
{
    Texture1D,
    Texture2D,
    Texture3D
};

struct ImageSubresource
{
    size_t offset = 0;
    size_t rowPitch = 0;
    size_t slicePitch = 0;
};

// Owns CPU pixel data, including compressed DDS blocks. Offsets remain valid
// when ImageData is copied or moved; no GPU device or resource is required.
struct ImageData
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t arraySize = 1;
    uint32_t mipLevels = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    ImageDimension dimension = ImageDimension::Texture2D;
    bool isCubeMap = false;
    std::vector<uint8_t> pixels;
    std::vector<ImageSubresource> subresources;
};
