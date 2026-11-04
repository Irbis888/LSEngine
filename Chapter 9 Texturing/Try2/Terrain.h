#pragma once
#include "Commons.h"
#include "ImageData.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <nlohmann/json.hpp>

struct TerrainLayer
{
    std::string albedo, normal;
    glm::vec3 color{1};
    float roughness = .8f, tileSize = 12;
};
struct TerrainComponent
{
    std::string source = "procedural", heightmap, weightmap;
    uint32_t samplesX = 2049, samplesZ = 2049, chunkCells = 64, seed = 7, lodCount = 5;
    float sizeX = 2048, sizeZ = 2048, heightScale = 180, heightOffset = -40;
    float frequency = .002f, lodDistance = 100, hysteresis = .15f, skirtDepth = 12;
    uint32_t layerCount = 4;
    std::array<TerrainLayer,4> layers = {{ {"","",{.18f,.32f,.08f},.9f,12},
        {"","",{.42f,.31f,.16f},.95f,10}, {"","",{.38f,.4f,.42f},.8f,16},
        {"","",{.88f,.9f,.92f},.65f,20} }};
    bool culling = true, wireframe = false, showLod = false, showBounds = false, collision = true;
    float friction = .5f, restitution = .05f;
    // Editor changes are staged until Rebuild; runtime IDs never enter scene JSON.
    uint64_t revision = 1;
};
struct TerrainChunk
{
    uint32_t x = 0, z = 0, cellsX = 0, cellsZ = 0;
    glm::vec3 min{}, max{};
    float skirt = 0;
};
struct TerrainGrid
{
    // x/z are sample offsets; y is 1 for a skirt vertex, 0 for a surface vertex.
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
};
struct TerrainResource
{
    TerrainComponent settings;
    std::vector<float> heights; // local metres, linear; one authoritative heightfield
    std::vector<uint8_t> weights; // normalized RGBA, whole-map UV
    std::vector<TerrainChunk> chunks;
    std::vector<TerrainGrid> grids;
    std::array<ImageData,8> images; // optional albedo/normal pairs, shared by all chunks
    double buildMs = 0;
    size_t Bytes() const;
    float Height(uint32_t x, uint32_t z) const;
    glm::vec3 Point(uint32_t x, uint32_t z) const;
    bool TryGetHeight(const TransformComponent&, float worldX, float worldZ, float& height) const;
    bool TryGetNormal(const TransformComponent&, float worldX, float worldZ, glm::vec3& normal) const;
    // Exact triangles at finest resolution, matching the GPU grid diagonal.
    bool Cell(float localX, float localZ, uint32_t& x, uint32_t& z, float& u, float& v) const;
};
struct TerrainDrawChunk { uint32_t chunk = 0, lod = 0; };
struct TerrainStats
{
    size_t visible = 0, culled = 0, triangles = 0, draws = 0, cpuBytes = 0, gpuBytes = 0;
    double buildMs = 0, selectMs = 0;
};
namespace Terrain
{
    void Validate(const TerrainComponent&);
    bool ValidTransform(const TransformComponent&);
    size_t EstimateBytes(const TerrainComponent&);
    std::shared_ptr<TerrainResource> Build(const TerrainComponent&, const std::filesystem::path& root,
        const std::atomic<bool>& cancelled);
    uint32_t SelectLod(float distance, uint32_t previous, const TerrainComponent&);
    nlohmann::json ToJson(const TerrainComponent&);
    TerrainComponent FromJson(const nlohmann::json&);
    // Tests all overlapped finest-resolution cells, not just the body's centre.
    bool Contact(const TerrainResource&, const TransformComponent& terrain, const AABB& body,
        ColliderType, glm::vec3& normal, float& penetration);
}
