#include "ResourceManager.h"

#include <algorithm>
#include <cmath>

//--------------------------------------------------------------
// ID генераторы
//--------------------------------------------------------------

static MeshID gNextMeshID = 1;
static MaterialID gNextMaterialID = 1;
static TextureID gNextTextureID = 1;

//--------------------------------------------------------------
// Mesh
//--------------------------------------------------------------

MeshID ResourceManager::CreateMesh(Mesh mesh)
{
    MeshID id = gNextMeshID++;
    mMeshes[id] = std::move(mesh);
    return id;
}

MeshID ResourceManager::LoadMesh(const std::string& path)
{
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_ConvertToLeftHanded |
        aiProcess_FlipUVs |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace);

    if (!scene || !scene->mRootNode)
    {
        throw std::runtime_error(importer.GetErrorString());
    }

    Mesh mesh;

    // 1. Сначала загрузим все материалы сцены
    std::vector<MaterialID> materialIDs(scene->mNumMaterials);

    for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
    {
        aiMaterial* aiMat = scene->mMaterials[i];

        Material mat;
        mat.name = aiMat->GetName().C_Str();

        aiString texPath;

        // --- DIFFUSE ---
        if (aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
        {
            std::wstring wpath(texPath.C_Str(), texPath.C_Str() + strlen(texPath.C_Str()));
            mat.albedo = LoadTexture(wpath);
        }

        // --- NORMAL ---
        if (aiMat->GetTexture(aiTextureType_DISPLACEMENT, 0, &texPath) == AI_SUCCESS)
        {
            std::wstring wpath(texPath.C_Str(), texPath.C_Str() + strlen(texPath.C_Str()));
            mat.normal = LoadTexture(wpath);
        }

        MaterialID matID = CreateMaterial(mat);
        materialIDs[i] = matID;
    }

    // 2. Грузим меши
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        aiMesh* aMesh = scene->mMeshes[m];

        Mesh::Submesh submesh;

        // старт индексов этого submesh в global index buffer
        submesh.indexOffset = static_cast<uint32_t>(mesh.indices.size());
        submesh.material = materialIDs[aMesh->mMaterialIndex];

        uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());

        // =========================
        // VERTICES
        // =========================
        for (unsigned int i = 0; i < aMesh->mNumVertices; ++i)
        {
            glm::vec3 pos(
                aMesh->mVertices[i].x,
                aMesh->mVertices[i].y,
                aMesh->mVertices[i].z
            );

            glm::vec3 normal(0.0f);
            if (aMesh->HasNormals())
            {
                normal = glm::vec3(
                    aMesh->mNormals[i].x,
                    aMesh->mNormals[i].y,
                    aMesh->mNormals[i].z
                );
            }

            glm::vec3 tangent(0.0f);
            if (aMesh->HasTangentsAndBitangents())
            {
                tangent = glm::vec3(
                    aMesh->mTangents[i].x,
                    aMesh->mTangents[i].y,
                    aMesh->mTangents[i].z
                );
            }

            glm::vec2 uv(0.0f);
            if (aMesh->HasTextureCoords(0) && aMesh->mTextureCoords[0])
            {
                uv = glm::vec2(
                    aMesh->mTextureCoords[0][i].x,
                    aMesh->mTextureCoords[0][i].y
                );
            }

            mesh.vertices.emplace_back(pos, normal, tangent, uv);
        }

        // =========================
        // INDICES
        // =========================
        uint32_t localIndexCount = 0;

        for (unsigned int i = 0; i < aMesh->mNumFaces; ++i)
        {
            const aiFace& face = aMesh->mFaces[i];

            // Assimp already triangulated
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
            {
                mesh.indices.push_back(baseVertex + face.mIndices[j]);
                localIndexCount++;
            }
        }

        submesh.indexCount = localIndexCount;

        mesh.submeshes.push_back(submesh);
    }

    return CreateMesh(std::move(mesh));
}

MeshID ResourceManager::CreatePlane(MaterialID material)
{
    Mesh mesh;

    mesh.vertices =
    {
        Vertex(glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(0.0f, 1.0f)),
        Vertex(glm::vec3(-0.5f, 0.0f,  0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
        Vertex(glm::vec3( 0.5f, 0.0f,  0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(1.0f, 0.0f)),
        Vertex(glm::vec3( 0.5f, 0.0f, -0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(1.0f, 1.0f))
    };

    mesh.indices = { 0, 1, 2, 0, 2, 3 };
    mesh.submeshes.push_back(Mesh::Submesh{ 0, static_cast<uint32_t>(mesh.indices.size()), material });

    return CreateMesh(std::move(mesh));
}

MeshID ResourceManager::CreateCube(MaterialID material)
{
    Mesh mesh;

    const glm::vec3 positions[8] =
    {
        {-0.5f, -0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f},
        { 0.5f, -0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f},
        { 0.5f, -0.5f,  0.5f}
    };

    const struct Face
    {
        uint32_t a;
        uint32_t b;
        uint32_t c;
        uint32_t d;
        glm::vec3 normal;
        glm::vec3 tangent;
    } faces[6] =
    {
        {4, 5, 6, 7, glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(1.0f, 0.0f,  0.0f)},
        {3, 2, 1, 0, glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)},
        {1, 5, 4, 0, glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 0.0f,  1.0f)},
        {6, 2, 3, 7, glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 0.0f, -1.0f)},
        {1, 2, 6, 5, glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(1.0f, 0.0f,  0.0f)},
        {4, 7, 3, 0, glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(1.0f, 0.0f,  0.0f)}
    };

    for (const Face& face : faces)
    {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

        mesh.vertices.emplace_back(positions[face.a], face.normal, face.tangent, glm::vec2(0.0f, 1.0f));
        mesh.vertices.emplace_back(positions[face.b], face.normal, face.tangent, glm::vec2(0.0f, 0.0f));
        mesh.vertices.emplace_back(positions[face.c], face.normal, face.tangent, glm::vec2(1.0f, 0.0f));
        mesh.vertices.emplace_back(positions[face.d], face.normal, face.tangent, glm::vec2(1.0f, 1.0f));

        mesh.indices.insert(mesh.indices.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }

    mesh.submeshes.push_back(Mesh::Submesh{ 0, static_cast<uint32_t>(mesh.indices.size()), material });

    return CreateMesh(std::move(mesh));
}

MeshID ResourceManager::CreateSphere(MaterialID material, uint32_t slices, uint32_t stacks)
{
    Mesh mesh;

    constexpr float pi = 3.14159265358979323846f;
    slices = std::max<uint32_t>(slices, 3);
    stacks = std::max<uint32_t>(stacks, 2);

    for (uint32_t stack = 0; stack <= stacks; ++stack)
    {
        const float v = static_cast<float>(stack) / static_cast<float>(stacks);
        const float phi = v * pi;
        const float y = 0.5f * std::cos(phi);
        const float ringRadius = 0.5f * std::sin(phi);

        for (uint32_t slice = 0; slice <= slices; ++slice)
        {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * 2.0f * pi;

            glm::vec3 position(
                ringRadius * std::sin(theta),
                y,
                ringRadius * std::cos(theta));

            glm::vec3 normal = glm::normalize(position);
            glm::vec3 tangent(std::cos(theta), 0.0f, -std::sin(theta));

            mesh.vertices.emplace_back(position, normal, tangent, glm::vec2(u, v));
        }
    }

    const uint32_t ringVertexCount = slices + 1;

    for (uint32_t stack = 0; stack < stacks; ++stack)
    {
        for (uint32_t slice = 0; slice < slices; ++slice)
        {
            const uint32_t a = stack * ringVertexCount + slice;
            const uint32_t b = (stack + 1) * ringVertexCount + slice;
            const uint32_t c = (stack + 1) * ringVertexCount + slice + 1;
            const uint32_t d = stack * ringVertexCount + slice + 1;

            mesh.indices.insert(mesh.indices.end(), { a, b, c, a, c, d });
        }
    }

    mesh.submeshes.push_back(Mesh::Submesh{ 0, static_cast<uint32_t>(mesh.indices.size()), material });

    return CreateMesh(std::move(mesh));
}


Mesh& ResourceManager::GetMesh(MeshID id)
{
    auto it = mMeshes.find(id);
    assert(it != mMeshes.end() && "Mesh not found!");
    return it->second;
}

//--------------------------------------------------------------
// Material
//--------------------------------------------------------------

MaterialID ResourceManager::CreateMaterial(const Material& mat)
{
    MaterialID id = gNextMaterialID++;
    mMaterials[id] = mat;
    return id;
}

Material& ResourceManager::GetMaterial(MaterialID id)
{
    auto it = mMaterials.find(id);
    assert(it != mMaterials.end() && "Material not found!");
    return it->second;
}

//--------------------------------------------------------------
// Texture
//--------------------------------------------------------------

TextureID ResourceManager::LoadTexture(const std::wstring& filename)
{
    Texture tex;

    tex.filename = filename;

    // имя можно вытащить из пути (пока просто копия)
    tex.name = std::string(filename.begin(), filename.end());

    TextureID id = gNextTextureID++;
    mTextures[id] = std::move(tex);

    return id;
}

Texture& ResourceManager::GetTexture(TextureID id)
{
    auto it = mTextures.find(id);
    assert(it != mTextures.end() && "Texture not found!");
    return it->second;
}



void ResourceManager::PrintAllMeshes() const
{
    std::cout << "==== MESHES ====\n";

    for (const auto& [id, mesh] : mMeshes)
    {
        std::cout << "MeshID: " << id << "\n";
        std::cout << "Vertices: " << mesh.vertices.size() << "\n";
        std::cout << "Indices: " << mesh.indices.size() << "\n";
        std::cout << "Submeshes: " << mesh.submeshes.size() << "\n";

        for (size_t i = 0; i < mesh.submeshes.size(); ++i)
        {
            const auto& sub = mesh.submeshes[i];

            std::cout << "  Submesh " << i << ":\n";
            std::cout << "    IndexOffset: " << sub.indexOffset << "\n";
            std::cout << "    IndexCount: " << sub.indexCount << "\n";
            std::cout << "    MaterialID: " << sub.material << "\n";
        }

        std::cout << "------------------------\n";
    }
}

void ResourceManager::PrintAllMaterials() const
{
    std::cout << "==== MATERIALS ====\n";

    for (const auto& [id, mat] : mMaterials)
    {
        std::cout << "MaterialID: " << id << "\n";
        std::cout << "Name: " << mat.name << "\n";

        std::cout << "Albedo: " << mat.albedo << "\n";
        std::cout << "Normal: " << mat.normal << "\n";

        std::cout << "Color: ("
            << mat.color.x << ", "
            << mat.color.y << ", "
            << mat.color.z << ")\n";

        std::cout << "Roughness: " << mat.roughness << "\n";

        std::cout << "------------------------\n";
    }
}

void ResourceManager::PrintAllTextures() const
{
    std::cout << "==== TEXTURES ====\n";

    for (const auto& [id, tex] : mTextures)
    {
        std::wcout << L"TextureID: " << id << L"\n";
        std::wcout << L"Name: " << tex.name.c_str() << L"\n";
        std::wcout << L"File: " << tex.filename << L"\n";
        std::wcout << L"------------------------\n";
    }
}
