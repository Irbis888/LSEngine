#pragma once

#include "Commons.h"
#include "ResourceManager.h"

enum class PrimitiveType
{
    Plane,
    Cube,
    Sphere
};

class SceneFactory
{
public:
    SceneFactory(entt::registry& registry, ResourceManager& resources);

    entt::entity CreateEmpty(
        const std::string& name,
        const glm::vec3& position = glm::vec3(0.0f),
        const glm::vec3& rotation = glm::vec3(0.0f),
        const glm::vec3& scale = glm::vec3(1.0f));

    entt::entity CreateMeshEntity(
        const std::string& name,
        MeshID mesh,
        const glm::vec3& position = glm::vec3(0.0f),
        const glm::vec3& rotation = glm::vec3(0.0f),
        const glm::vec3& scale = glm::vec3(1.0f));

    entt::entity CreatePrimitive(
        const std::string& name,
        PrimitiveType type,
        MaterialID material,
        const glm::vec3& position = glm::vec3(0.0f),
        const glm::vec3& rotation = glm::vec3(0.0f),
        const glm::vec3& scale = glm::vec3(1.0f));

    entt::entity CreateCamera(
        const std::string& name,
        const glm::vec3& position,
        const glm::vec3& rotation,
        float fov = 1.8f,
        float nearZ = 0.1f,
        float farZ = 1500.0f,
        float aspectRatio = 4.0f / 3.0f);

private:
    MeshID CreatePrimitiveMesh(PrimitiveType type, MaterialID material);

private:
    entt::registry& mRegistry;
    ResourceManager& mResources;
};
