#include "ResourceManager.h"

//--------------------------------------------------------------
// ID генераторы
//--------------------------------------------------------------

static MeshID gNextMeshID = 1;
static MaterialID gNextMaterialID = 1;
static TextureID gNextTextureID = 1;

//--------------------------------------------------------------
// Mesh
//--------------------------------------------------------------

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

    MeshID id = gNextMeshID++;
    mMeshes[id] = std::move(mesh);

    return id;
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