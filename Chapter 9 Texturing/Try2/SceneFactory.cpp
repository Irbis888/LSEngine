#include "SceneFactory.h"

#include <stdexcept>

SceneFactory::SceneFactory(entt::registry& registry, ResourceManager& resources)
    : mRegistry(registry),
      mResources(resources)
{
}

entt::entity SceneFactory::CreateEmpty(
    const std::string& name,
    const glm::vec3& position,
    const glm::vec3& rotation,
    const glm::vec3& scale)
{
    entt::entity entity = mRegistry.create();
    mRegistry.emplace<TagComponent>(entity, TagComponent{ name });
    mRegistry.emplace<TransformComponent>(entity, TransformComponent{ position, rotation, scale });

    return entity;
}

entt::entity SceneFactory::CreateMeshEntity(
    const std::string& name,
    MeshID mesh,
    const glm::vec3& position,
    const glm::vec3& rotation,
    const glm::vec3& scale)
{
    entt::entity entity = CreateEmpty(name, position, rotation, scale);
    mRegistry.emplace<MeshComponent>(entity, MeshComponent{ mesh });

    return entity;
}

entt::entity SceneFactory::CreatePrimitive(
    const std::string& name,
    PrimitiveType type,
    MaterialID material,
    const glm::vec3& position,
    const glm::vec3& rotation,
    const glm::vec3& scale)
{
    MeshID mesh = CreatePrimitiveMesh(type, material);
    return CreateMeshEntity(name, mesh, position, rotation, scale);
}

entt::entity SceneFactory::CreateCamera(
    const std::string& name,
    const glm::vec3& position,
    const glm::vec3& rotation,
    float fov,
    float nearZ,
    float farZ,
    float aspectRatio)
{
    entt::entity entity = CreateEmpty(name, position, rotation, glm::vec3(1.0f));
    mRegistry.emplace<CameraComponent>(
        entity,
        CameraComponent{ fov, nearZ, farZ, aspectRatio });

    return entity;
}

MeshID SceneFactory::CreatePrimitiveMesh(PrimitiveType type, MaterialID material)
{
    switch (type)
    {
    case PrimitiveType::Plane:
        return mResources.CreatePlane(material);
    case PrimitiveType::Cube:
        return mResources.CreateCube(material);
    case PrimitiveType::Sphere:
        return mResources.CreateSphere(material);
    default:
        throw std::runtime_error("Unknown primitive type");
    }
}
