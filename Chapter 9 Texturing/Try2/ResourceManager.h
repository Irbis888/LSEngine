#pragma once
#include "Commons.h"


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
};

struct Material
{
    std::string name;
    // позже:
    // TextureID albedo;
    // ShaderID shader;
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
    MaterialID CreateMaterial(const Material& mat);

    Mesh& GetMesh(MeshID id);
    Material& GetMaterial(MaterialID id);

private:
    std::unordered_map<MeshID, Mesh> mMeshes;
    std::unordered_map<MaterialID, Material> mMaterials;
    std::unordered_map<TextureID, Texture> mTextures;
};
