#pragma once
#include "Commons.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>


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
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    struct Submesh
    {
        uint32_t indexOffset;
        uint32_t indexCount;
        MaterialID material;
    };

    std::vector<Submesh> submeshes;
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
    
    // позже:
    // TextureID albedo;
    // ShaderID shader;
};


class ResourceManager
{
public:
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

    Mesh& GetMesh(MeshID id);
    Material& GetMaterial(MaterialID id);

    TextureID LoadTexture(const std::wstring& filename);
    Texture& ResourceManager::GetTexture(TextureID id);

    void PrintAllMeshes() const;
    void PrintAllMaterials() const;
    void PrintAllTextures() const;

private:
    std::unordered_map<MeshID, Mesh> mMeshes;
    std::unordered_map<MaterialID, Material> mMaterials;
    std::unordered_map<TextureID, Texture> mTextures;
};
