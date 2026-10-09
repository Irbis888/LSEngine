#pragma once
#include "Commons.h"
#include "ImageData.h"
#include <deque>

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

struct Mesh
{
    ResourceState state = ResourceState::CpuReady;
    // Generated primitives upload lazily on their first draw, without the model queue.
    bool isPrimitive = false;
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
};

struct MaterialDesc
{
    std::string name;
    std::wstring albedoTexture;
    std::wstring normalTexture;
    glm::vec3 color = glm::vec3(1.0f);
    float roughness = 0.5f;
};

struct Material
{
    std::string name;

    TextureID albedo = 0;
    TextureID normal = 0;

    // простые параметры (пока)
    glm::vec3 color = glm::vec3(1.0f);
    float roughness = 0.5f;
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
    ResourceManager();
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // Main-thread API. Workers produce isolated results; PumpLoading publishes
    // completed results without waiting and admits a bounded number of jobs.
    // The JobSystem must outlive this manager, or call ShutdownLoading first.
    void InitLoading(JobSystem& jobs, uint32_t maxConcurrentLoads = 4);
    void PumpLoading();
    void ShutdownLoading();
    bool HasPendingLoads() const;
    size_t GetActiveLoadCount() const;
    size_t GetQueuedLoadCount() const;
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
