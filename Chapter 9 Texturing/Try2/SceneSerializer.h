#pragma once

#include <string>
#include <optional>

#include "ResourceManager.h"
#include "World.h"

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
    };
    struct SceneData { std::vector<EntityDescription> entities; };

    // CPU-only file reading, JSON parsing and validation, safe inside a job.
    static SceneData Read(const std::string& path);
    // Only the main thread may publish entities and request resources.
    static void CreateEntity(World& world, ResourceManager& resources, const EntityDescription& entity);
    static void Save(const World& world, const std::string& path);
    static void Load(World& world, const std::string& path);
    static void Load(World& world, ResourceManager& resources, const std::string& path);
};
