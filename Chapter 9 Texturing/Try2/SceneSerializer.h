#pragma once

#include <string>
#include <optional>

#include "ResourceManager.h"
#include "World.h"
#include "ScriptComponent.h"
#include <filesystem>

class SceneSerializer
{
public:
    struct MeshDescription
    {
        std::optional<MeshID> existing;
        std::string source;
        std::string primitive;
        std::string path;
        std::optional<MaterialDesc> material;
    };
    struct EntityDescription
    {
        std::string tag;
        TransformComponent transform;
        std::optional<MeshDescription> mesh;
        std::optional<CameraComponent> camera;
        std::optional<DirectionalLightComponent> directionalLight;
        std::optional<PointLightComponent> pointLight;
        std::optional<SpotLightComponent> spotLight;
        std::optional<RigidbodyComponent> rigidbody;
        std::optional<ColliderComponent> collider;
        std::optional<ScriptComponent> script;
    };
    struct SceneData { std::vector<EntityDescription> entities; };

    // CPU-only file reading, JSON parsing and validation, safe inside a job.
    static SceneData Read(const std::string& path);
    // One entity object in the scene-entity format (tag defaults to defaultTag). CPU-only.
    static EntityDescription ReadPrefab(const std::filesystem::path& file, const std::string& defaultTag);
    // Only the main thread may publish entities and request resources.
    static void CreateEntity(World& world, ResourceManager& resources, const EntityDescription& entity);
    // Adds the described components to an existing, still empty entity.
    static void Instantiate(entt::registry& registry, entt::entity entity, ResourceManager& resources,
        const EntityDescription& description);
    // Meshes are written as their source description (portable between runs). keepRuntimeMeshIds
    // also writes the session mesh id, which Play snapshots use to avoid re-creating resources.
    // Returns how many meshes had no source and could only be written as a session id.
    static size_t Save(const World& world, const std::string& path, const std::filesystem::path& assetRoot = {},
        bool keepRuntimeMeshIds = false);
    static void Load(World& world, const std::string& path);
    static void Load(World& world, ResourceManager& resources, const std::string& path);
};

// How an entity's mesh was described (primitive/model + material), kept next to the runtime
// MeshComponent so the editor can save scenes that load again after a restart.
struct MeshSourceComponent
{
    SceneSerializer::MeshDescription mesh;
};
