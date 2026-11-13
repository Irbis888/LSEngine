#pragma once
#include <compare>
#include <optional>
#include "Commons.h"
#include "ImageData.h"
#include <deque>
#include <filesystem>

class JobSystem;
enum class ResourceState { Loading, CpuReady, Ready, Failed };


struct Vertex
{
    Vertex() {}
    Vertex(
        const glm::vec3& p,
        const glm::vec3& n,
        const glm::vec3& t,
        const glm::vec2& uv) :
        Position(p),
        Normal(n),
        TangentU(t),
        TexC(uv) {
    }
    Vertex(
        float px, float py, float pz,
        float nx, float ny, float nz,
        float tx, float ty, float tz,
        float u, float v) :
        Position(px, py, pz),
        Normal(nx, ny, nz),
        TangentU(tx, ty, tz),
        TexC(u, v) {
    }

    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec3 TangentU;
    glm::vec2 TexC;
};

struct MeshGeometry
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct Mesh
{
    ResourceState state = ResourceState::CpuReady;
    // Generated primitives upload lazily on their first draw, without the model queue.
    bool isPrimitive = false;
    std::shared_ptr<const MeshGeometry> sharedGeometry;
    std::string error;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    struct Submesh
    {
        uint32_t indexOffset;
        uint32_t indexCount;
        MaterialID material;
    };

    std::vector<Submesh> submeshes;
    uint32_t materialVersion = 1;
    const std::vector<Vertex>& GetVertices() const { return sharedGeometry ? sharedGeometry->vertices : vertices; }
    const std::vector<uint32_t>& GetIndices() const { return sharedGeometry ? sharedGeometry->indices : indices; }
    size_t GeometryBytes() const { return GetVertices().size() * sizeof(Vertex) + GetIndices().size() * sizeof(uint32_t); }
};

enum class SurfaceMode
{
    Opaque,
    Masked,
    Translucent
};

struct SurfaceProgram
{
    std::wstring file = L"Shaders/Default.hlsl";
    std::string vertexEntry = "VS";
    std::string pixelEntry = "PS";
    bool editorVariant = true;
    bool maskedVariant = true;

    auto operator<=>(const SurfaceProgram&) const = default;
};

struct MaterialParameters
{
    glm::vec4 baseColor{1.0f};
    TextureID albedo = 0;
    TextureID normal = 0;
    TextureID orm = 0;
    TextureID emissive = 0;
    float roughness = 0.5f;
    std::optional<float> metallic;
    float normalStrength = 1.0f;
    float aoStrength = 1.0f;
    float alphaCutoff = 0.5f;
    glm::vec3 emissiveColor{0.0f};
    float emissiveIntensity = 1.0f;
    glm::vec2 uvScale{1.0f};
    glm::vec2 uvOffset{0.0f};
};

struct MaterialBase
{
    SurfaceProgram program;
    SurfaceMode mode = SurfaceMode::Opaque;
    bool twoSided = false;
    MaterialParameters defaults;
};

struct MaterialDesc
{
    std::string name;
    std::wstring albedoTexture;
    std::wstring normalTexture;
    glm::vec3 color = glm::vec3(1.0f);
    float roughness = 0.5f;
    std::optional<MaterialBase> surface;
    std::wstring ormTexture;
    std::wstring emissiveTexture;
};

struct Material
{
    explicit Material(std::shared_ptr<const MaterialBase> definition = {})
        : base(std::move(definition)), parameters(base ? base->defaults : MaterialParameters{})
    {
    }

    std::string name;
    std::shared_ptr<const MaterialBase> base;
    MaterialParameters parameters;
};

struct Texture
{
    std::string name;
    std::wstring filename;
    ImageData imageData;
    ResourceState state = ResourceState::CpuReady;
    std::string error;
};


class ResourceManager
{
public:
    // CPU-only decoder shared by specialized assets; no resource IDs or GPU work.
    // DDS, PNG and TGA (plus other stb formats); PNG/TGA get a generated mip chain.
    static ImageData DecodeImage(const std::filesystem::path& file);
    // Writes the top mip of a 2D image as PNG or TGA, chosen by extension.
    static void SaveImage(const std::filesystem::path& file, const ImageData& image);
    // Saves a loaded texture's CPU pixels (state CpuReady or Ready) as PNG or TGA.
    void SaveTexture(TextureID id, const std::filesystem::path& file);
    ResourceManager();
    ~ResourceManager();
    // Where relative model/texture paths point: the project folder, then (textures only)
    // the project's texture search folders. Without a root, paths are relative to the
    // current directory. Call before loading resources.
    struct AssetSearch
    {
        std::filesystem::path root;
        std::vector<std::filesystem::path> textureDirectories;
    };
    void SetAssetRoot(const std::filesystem::path& root, const std::vector<std::filesystem::path>& textureDirectories = {});
    const AssetSearch& GetAssetSearch() const { return *mSearch; }
    std::filesystem::path ResolveAssetPath(const std::string& path) const;
    std::wstring ResolveTexture(const std::wstring& filename) const;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // Main-thread API. Workers produce isolated results; PumpLoading publishes
    // completed results without waiting. Consumers drain requests independently of frames.
    // The JobSystem must outlive this manager, or call ShutdownLoading first.
    // Admission is also capped at workers - 1 (minimum 1), reserving capacity for physics.
    void InitLoading(JobSystem& jobs, uint32_t maxConcurrentLoads = 4);
    void PumpLoading(double maxMilliseconds = 4.0);
    void ShutdownLoading();
    bool HasPendingLoads() const;
    size_t GetActiveLoadCount() const;
    size_t GetQueuedLoadCount() const;
    struct LoadingStats
    {
        size_t readyBytes = 0, peakReadyBytes = 0, maxReadyBytes = 0;
        size_t completedResults = 0, activeLoads = 0;
    };
    LoadingStats GetLoadingStats() const;
    void SetLoadingMemoryBudget(size_t maxReadyBytes);
    void FinishMeshUpload(MeshID id);
    struct TextureProgress { size_t total = 0, ready = 0, failed = 0; };
    TextureProgress GetTextureProgress() const;
    TextureID GetPlaceholderTexture() const { return mPlaceholderTexture; }
    TextureID GetWhiteTexture() const { return mWhiteTexture; }
    TextureID GetFlatNormalTexture() const { return mFlatNormalTexture; }
    TextureID PeekTextureUpload();
    void FinishTextureUpload(TextureID id, const std::string& error = {});

    MeshID LoadMesh(const std::string& path);
    MeshID CreateMesh(Mesh mesh);
    MeshID CreatePlane(MaterialID material);
    MeshID CreateCube(MaterialID material);
    MeshID CreateSphere(MaterialID material, uint32_t slices = 32, uint32_t stacks = 16);
    MaterialID CreateMaterial(const Material& mat);
    MaterialID CreateSolidMaterial(
        const std::string& name,
        const glm::vec3& color,
        float roughness = 0.5f);
    MaterialID CreateTexturedMaterial(const MaterialDesc& desc);
    MaterialID CreateTexturedMaterial(
        const std::string& name,
        const std::wstring& albedoTexture,
        const std::wstring& normalTexture = L"",
        const glm::vec3& color = glm::vec3(1.0f),
        float roughness = 0.5f);

    void SetMeshMaterial(MeshID mesh, MaterialID material);
    void SetSubmeshMaterial(MeshID mesh, uint32_t submeshIndex, MaterialID material);

    Mesh& GetMesh(MeshID id);
    Material& GetMaterial(MaterialID id);

    TextureID LoadTexture(const std::wstring& filename);
    Texture& GetTexture(TextureID id);

    void PrintAllMeshes() const;
    void PrintAllMaterials() const;
    void PrintAllTextures() const;

private:
    struct LoadingState;
    std::unique_ptr<LoadingState> mLoading;
    std::shared_ptr<const AssetSearch> mSearch = std::make_shared<AssetSearch>();
    std::unordered_map<TextureID, size_t> mPendingTextureBytes;
    std::unordered_map<MeshID, size_t> mPendingMeshBytes;
    MeshID CreatePrimitive(std::shared_ptr<const MeshGeometry> geometry, MaterialID material);
    std::shared_ptr<const MeshGeometry> mPlaneGeometry, mCubeGeometry;
    std::unordered_map<uint64_t, std::shared_ptr<const MeshGeometry>> mSphereGeometries;
    TextureID CreateBuiltinTexture(const std::string& name, uint32_t width,
        uint32_t height, std::vector<uint8_t> pixels);
    TextureID mPlaceholderTexture = 0;
    TextureID mWhiteTexture = 0;
    TextureID mFlatNormalTexture = 0;
    std::deque<TextureID> mTextureUploads;
    std::unordered_map<MeshID, MaterialID> mMeshMaterialOverrides;
    std::unordered_map<MeshID, std::unordered_map<uint32_t, MaterialID>> mSubmeshMaterialOverrides;
    std::unordered_map<MeshID, Mesh> mMeshes;
    std::unordered_map<MaterialID, Material> mMaterials;
    std::unordered_map<TextureID, Texture> mTextures;
    std::unordered_map<std::wstring, TextureID> mTextureIDsByFilename;
};
