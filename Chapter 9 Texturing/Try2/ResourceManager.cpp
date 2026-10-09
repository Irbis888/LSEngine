#include "ResourceManager.h"
#include "JobSystem.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <stdexcept>
#include "DDSTextureLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"

//--------------------------------------------------------------
// ID генераторы
//--------------------------------------------------------------

static MeshID gNextMeshID = 1;
static MaterialID gNextMaterialID = 1;
static TextureID gNextTextureID = 1;

namespace
{
    std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return {};
        }

        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring ResolveTexturePath(const std::wstring& filename)
    {
        namespace fs = std::filesystem;
        fs::path path(filename);
        if (!path.is_absolute() && !fs::exists(path))
        {
            auto firstPart = path.begin();
            if (firstPart != path.end() && firstPart->wstring() != L"..")
                path = fs::path(L"../../Textures") / path;
        }
        return fs::absolute(path).lexically_normal().wstring();
    }

    ImageData LoadImage(const std::wstring& filename)
    {
        ZoneScopedN("Texture CPU read and decode");
        ImageData image;
        std::wstring extension = std::filesystem::path(filename).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (extension == L".dds")
        {
            const HRESULT hr = DirectX::LoadDDSImageFromFile(filename.c_str(), image);
            if (FAILED(hr))
                throw std::runtime_error("Failed to load DDS texture: " + WideToUtf8(filename) +
                    " (HRESULT " + std::to_string(static_cast<uint32_t>(hr)) + ")");
            return image;
        }

        FILE* handle = nullptr;
        if (_wfopen_s(&handle, filename.c_str(), L"rb") != 0)
            throw std::runtime_error("Failed to open texture: " + WideToUtf8(filename));
        std::unique_ptr<FILE, decltype(&std::fclose)> file(handle, &std::fclose);
        int width = 0;
        int height = 0;
        int channels = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_file(file.get(), &width, &height, &channels, STBI_rgb_alpha),
            &stbi_image_free);
        if (!pixels)
            throw std::runtime_error("Failed to decode texture: " + WideToUtf8(filename) +
                " (" + stbi_failure_reason() + ")");
        image.width = static_cast<uint32_t>(width);
        image.height = static_cast<uint32_t>(height);
        image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        const size_t rowPitch = static_cast<size_t>(width) * 4;
        const size_t slicePitch = rowPitch * static_cast<size_t>(height);
        image.pixels.assign(pixels.get(), pixels.get() + slicePitch);
        image.subresources.push_back({ 0, rowPitch, slicePitch });
        return image;
    }
}

namespace
{
    struct DecodedModel
    {
        Mesh mesh;
        std::vector<MaterialDesc> materials;
    };

    DecodedModel DecodeModel(const std::string& path)
    {
        ZoneScopedN("Model CPU read and decode");
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

        DecodedModel result;
        Mesh& mesh = result.mesh;

        // 1. Сначала загрузим все материалы сцены
        result.materials.resize(scene->mNumMaterials);
        const auto modelDirectory = std::filesystem::path(path).parent_path();
        const auto resolveMaterialTexture = [&](const aiString& texturePath)
        {
            const auto relativePath = std::filesystem::path(std::u8string(
                reinterpret_cast<const char8_t*>(texturePath.C_Str()), texturePath.length));
            const auto modelPath = modelDirectory / relativePath;
            return ResolveTexturePath((std::filesystem::exists(modelPath) ? modelPath : relativePath).wstring());
        };

        for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
        {
            aiMaterial* aiMat = scene->mMaterials[i];

            MaterialDesc& mat = result.materials[i];
            mat.name = aiMat->GetName().C_Str();

            aiString texPath;

            // --- DIFFUSE ---
            if (aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
            {
                mat.albedoTexture = resolveMaterialTexture(texPath);
            }

            // --- NORMAL ---
            if (aiMat->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS ||
                aiMat->GetTexture(aiTextureType_HEIGHT, 0, &texPath) == AI_SUCCESS ||
                aiMat->GetTexture(aiTextureType_DISPLACEMENT, 0, &texPath) == AI_SUCCESS)
            {
                mat.normalTexture = resolveMaterialTexture(texPath);
            }


        }

        // 2. Грузим меши

        for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
        {
            aiMesh* aMesh = scene->mMeshes[m];

            Mesh::Submesh submesh;

            // старт индексов этого submesh в global index buffer
            submesh.indexOffset = static_cast<uint32_t>(mesh.indices.size());
            submesh.material = aMesh->mMaterialIndex; // Local material index until main-thread publication.

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

        return result;
    }

}

struct ResourceManager::LoadingState
{
    struct Request
    {
        bool texture;
        uint32_t id;
        std::string modelPath;
        std::wstring texturePath;
    };
    struct Result
    {
        ImageData image;
        DecodedModel model;
        std::string error;
    };
    struct Active
    {
        Request request;
        std::shared_ptr<Result> result;
        JobSystem::TaskHandle task;
    };
    JobSystem* jobs = nullptr;
    uint32_t concurrency = 1;
    bool stopped = false;
    std::deque<Request> queued;
    std::vector<Active> active;
};

ResourceManager::ResourceManager()
{
    // Available before starting any jobs; no file access or decoder required.
    mPlaceholderTexture = CreateBuiltinTexture("Loading checkerboard", 2, 2,
        { 255, 0, 255, 255, 32, 32, 32, 255,
          32, 32, 32, 255, 255, 0, 255, 255 });
    mWhiteTexture = CreateBuiltinTexture("White", 1, 1, { 255, 255, 255, 255 });
    mFlatNormalTexture = CreateBuiltinTexture("Flat normal", 1, 1, { 128, 128, 255, 255 });
}

ResourceManager::~ResourceManager()
{
    ShutdownLoading();
}

TextureID ResourceManager::CreateBuiltinTexture(const std::string& name,
    uint32_t width, uint32_t height, std::vector<uint8_t> pixels)
{
    Texture texture;
    texture.name = name;
    texture.imageData.width = width;
    texture.imageData.height = height;
    texture.imageData.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.imageData.pixels = std::move(pixels);
    texture.imageData.subresources.push_back({ 0, size_t(width) * 4, size_t(width) * height * 4 });
    const TextureID id = gNextTextureID++;
    mTextures.emplace(id, std::move(texture));
    // Renderer uploads these three textures before pumping regular resources.
    return id;
}

void ResourceManager::InitLoading(JobSystem& jobs, uint32_t maxConcurrentLoads)
{
    if (mLoading) throw std::logic_error("Resource loading is already initialized");
    if (!jobs.IsInitialized()) throw std::logic_error("JobSystem must be initialized first");
    mLoading = std::make_unique<LoadingState>();
    mLoading->jobs = &jobs;
    // Keep admission well below enkiTS pipe capacity, avoiding inline decoder
    // execution on Submit. The engine reserves a core for the rendering thread.
    mLoading->concurrency = std::clamp<uint32_t>(maxConcurrentLoads, 1,
        std::min<uint32_t>(jobs.GetWorkerThreadCount(), 8));
}

void ResourceManager::PumpLoading()
{
    if (!mLoading || mLoading->stopped) return;
    ZoneScopedN("Publish CPU resources and schedule jobs");
    auto& loading = *mLoading;
    for (size_t i = 0; i < loading.active.size();)
    {
        auto& active = loading.active[i];
        if (!active.task.IsComplete()) { ++i; continue; }
        // IsComplete synchronizes with worker writes. Wait cannot execute an
        // unfinished task here, and workers never access the resource maps.
        loading.jobs->Wait(active.task);
        const uint32_t id = active.request.id;
        auto& result = *active.result;
        if (active.request.texture)
        {
            auto& texture = GetTexture(id);
            texture.error = std::move(result.error);
            texture.state = texture.error.empty() ? ResourceState::CpuReady : ResourceState::Failed;
            if (texture.error.empty())
            {
                texture.imageData = std::move(result.image);
                mTextureUploads.push_back(id);
            }
            else std::cerr << texture.error << '\n';
        }
        else
        {
            auto& mesh = GetMesh(id);
            if (!result.error.empty())
            {
                mesh.state = ResourceState::Failed;
                mesh.error = std::move(result.error);
                std::cerr << mesh.error << '\n';
            }
            else
            {
                std::vector<MaterialID> materials;
                for (const auto& desc : result.model.materials)
                    materials.push_back(CreateTexturedMaterial(desc));
                for (size_t submeshIndex = 0; submeshIndex < result.model.mesh.submeshes.size(); ++submeshIndex)
                {
                    auto& submesh = result.model.mesh.submeshes[submeshIndex];
                    submesh.material = materials.at(submesh.material);
                    if (auto override = mMeshMaterialOverrides.find(id); override != mMeshMaterialOverrides.end())
                        submesh.material = override->second;
                    if (auto overrides = mSubmeshMaterialOverrides.find(id); overrides != mSubmeshMaterialOverrides.end())
                        if (auto override = overrides->second.find(uint32_t(submeshIndex)); override != overrides->second.end())
                            submesh.material = override->second;
                }
                mesh = std::move(result.model.mesh);
            }
            mMeshMaterialOverrides.erase(id);
            mSubmeshMaterialOverrides.erase(id);
        }
        loading.active.erase(loading.active.begin() + i);
    }

    // Backpressure bounds decoded data awaiting GPU finalization. A single
    // image may exceed the soft byte limit; in-flight results add at most
    // concurrency images. CPU copies already uploaded are retained as before.
    size_t readyBytes = 0;
    size_t readyCount = 0;
    for (const auto& [id, texture] : mTextures)
        if (texture.state == ResourceState::CpuReady && !texture.filename.empty())
        { readyBytes += texture.imageData.pixels.size(); ++readyCount; }
    while (!loading.queued.empty() && loading.active.size() < loading.concurrency &&
        readyCount + loading.active.size() < 32 && readyBytes < 64 * 1024 * 1024)
    {
        auto request = std::move(loading.queued.front());
        loading.queued.pop_front();
        auto result = std::make_shared<LoadingState::Result>();
        auto task = loading.jobs->Submit([request, result]
        {
            try
            {
                if (request.texture) result->image = LoadImage(request.texturePath);
                else result->model = DecodeModel(request.modelPath);
            }
            catch (const std::exception& error) { result->error = error.what(); }
            catch (...) { result->error = "Unknown CPU resource loading error"; }
        });
        loading.active.push_back({ std::move(request), std::move(result), std::move(task) });
    }
    TracyPlot("Resource jobs active", int64_t(loading.active.size()));
    TracyPlot("Resource requests queued", int64_t(loading.queued.size()));
    TracyPlot("Textures awaiting GPU", int64_t(readyCount));
}

void ResourceManager::ShutdownLoading()
{
    if (!mLoading || mLoading->stopped) return;
    auto& loading = *mLoading;
    loading.stopped = true;
    // Cancel work not submitted. Submitted callbacks capture only their own
    // request/result, so never retain a ResourceManager or renderer pointer.
    for (const auto& request : loading.queued)
    {
        if (request.texture)
        {
            auto& texture = GetTexture(request.id);
            texture.state = ResourceState::Failed;
            texture.error = "Loading cancelled during shutdown";
        }
        else
        {
            auto& mesh = GetMesh(request.id);
            mesh.state = ResourceState::Failed;
            mesh.error = "Loading cancelled during shutdown";
        }
    }
    loading.queued.clear();
    for (const auto& active : loading.active)
    {
        loading.jobs->Wait(active.task);
        if (active.request.texture)
        {
            auto& texture = GetTexture(active.request.id);
            texture.state = ResourceState::Failed;
            texture.error = "Loading cancelled during shutdown";
        }
        else
        {
            auto& mesh = GetMesh(active.request.id);
            mesh.state = ResourceState::Failed;
            mesh.error = "Loading cancelled during shutdown";
        }
    }
    loading.active.clear();
}

bool ResourceManager::HasPendingLoads() const
{
    return mLoading && (!mLoading->queued.empty() || !mLoading->active.empty());
}
size_t ResourceManager::GetActiveLoadCount() const { return mLoading ? mLoading->active.size() : 0; }
size_t ResourceManager::GetQueuedLoadCount() const { return mLoading ? mLoading->queued.size() : 0; }
ResourceManager::TextureProgress ResourceManager::GetTextureProgress() const
{
    TextureProgress progress;
    for (const auto& [id, texture] : mTextures)
    {
        if (texture.filename.empty()) continue;
        ++progress.total;
        if (texture.state == ResourceState::Ready) ++progress.ready;
        if (texture.state == ResourceState::Failed) ++progress.failed;
    }
    return progress;
}

TextureID ResourceManager::PeekTextureUpload()
{
    while (!mTextureUploads.empty() && GetTexture(mTextureUploads.front()).state != ResourceState::CpuReady)
        mTextureUploads.pop_front();
    return mTextureUploads.empty() ? 0 : mTextureUploads.front();
}

void ResourceManager::FinishTextureUpload(TextureID id, const std::string& error)
{
    auto& texture = GetTexture(id);
    texture.state = error.empty() ? ResourceState::Ready : ResourceState::Failed;
    texture.error = error;
    if (!error.empty()) std::cerr << texture.name << ": " << error << '\n';
}

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
    if (mLoading)
    {
        if (mLoading->stopped) throw std::logic_error("Resource loading has stopped");
        Mesh mesh;
        mesh.state = ResourceState::Loading;
        const MeshID id = CreateMesh(std::move(mesh));
        mLoading->queued.push_back({ false, id, std::filesystem::absolute(path).string(), {} });
        return id;
    }
    auto decoded = DecodeModel(path);
    std::vector<MaterialID> materials;
    for (const auto& desc : decoded.materials) materials.push_back(CreateTexturedMaterial(desc));
    for (auto& submesh : decoded.mesh.submeshes) submesh.material = materials.at(submesh.material);
    return CreateMesh(std::move(decoded.mesh));
}

MeshID ResourceManager::CreatePlane(MaterialID material)
{
    Mesh mesh;
    mesh.isPrimitive = true;

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
    mesh.isPrimitive = true;

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
    mesh.isPrimitive = true;

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

MaterialID ResourceManager::CreateSolidMaterial(
    const std::string& name,
    const glm::vec3& color,
    float roughness)
{
    Material mat;
    mat.name = name;
    mat.color = color;
    mat.roughness = roughness;

    return CreateMaterial(mat);
}

MaterialID ResourceManager::CreateTexturedMaterial(const MaterialDesc& desc)
{
    Material mat;
    mat.name = desc.name;
    mat.color = desc.color;
    mat.roughness = desc.roughness;

    if (!desc.albedoTexture.empty())
    {
        mat.albedo = LoadTexture(desc.albedoTexture);
    }

    if (!desc.normalTexture.empty())
    {
        mat.normal = LoadTexture(desc.normalTexture);
    }

    return CreateMaterial(mat);
}

MaterialID ResourceManager::CreateTexturedMaterial(
    const std::string& name,
    const std::wstring& albedoTexture,
    const std::wstring& normalTexture,
    const glm::vec3& color,
    float roughness)
{
    MaterialDesc desc;
    desc.name = name;
    desc.albedoTexture = albedoTexture;
    desc.normalTexture = normalTexture;
    desc.color = color;
    desc.roughness = roughness;

    return CreateTexturedMaterial(desc);
}

void ResourceManager::SetMeshMaterial(MeshID meshId, MaterialID materialId)
{
    GetMaterial(materialId);

    Mesh& mesh = GetMesh(meshId);
    if (mesh.state == ResourceState::Loading)
    {
        mMeshMaterialOverrides[meshId] = materialId;
        mSubmeshMaterialOverrides.erase(meshId);
    }
    for (Mesh::Submesh& submesh : mesh.submeshes)
    {
        submesh.material = materialId;
    }

    ++mesh.materialVersion;
}

void ResourceManager::SetSubmeshMaterial(MeshID meshId, uint32_t submeshIndex, MaterialID materialId)
{
    GetMaterial(materialId);

    Mesh& mesh = GetMesh(meshId);
    if (mesh.state == ResourceState::Loading)
    {
        mSubmeshMaterialOverrides[meshId][submeshIndex] = materialId;
        return;
    }
    if (submeshIndex >= mesh.submeshes.size())
    {
        throw std::out_of_range("Submesh index out of range");
    }

    mesh.submeshes[submeshIndex].material = materialId;
    ++mesh.materialVersion;
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
    if (filename.empty())
        throw std::invalid_argument("Texture filename is empty");
    const std::wstring resolvedFilename = ResolveTexturePath(filename);
    auto cached = mTextureIDsByFilename.find(resolvedFilename);
    if (cached != mTextureIDsByFilename.end())
    {
        return cached->second;
    }

    Texture tex;

    tex.filename = filename;
    if (mLoading)
    {
        if (mLoading->stopped) throw std::logic_error("Resource loading has stopped");
        tex.state = ResourceState::Loading;
    }
    else tex.imageData = LoadImage(resolvedFilename);

    // имя можно вытащить из пути (пока просто копия)
    tex.name = WideToUtf8(filename);

    TextureID id = gNextTextureID++;
    mTextures[id] = std::move(tex);
    mTextureIDsByFilename[resolvedFilename] = id;
    if (mLoading) mLoading->queued.push_back({ true, id, {}, resolvedFilename });
    else mTextureUploads.push_back(id);

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
