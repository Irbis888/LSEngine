#include "SceneSerializer.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "Commons.h"

using nlohmann::json;

namespace
{
    json Vec3ToJson(const glm::vec3& value)
    {
        return json::array({ value.x, value.y, value.z });
    }

    glm::vec3 JsonToVec3(const json& value, const glm::vec3& fallback = glm::vec3(0.0f))
    {
        if (!value.is_array() || value.size() != 3)
            return fallback;

        return glm::vec3(
            value.at(0).get<float>(),
            value.at(1).get<float>(),
            value.at(2).get<float>());
    }

    json TransformToJson(const TransformComponent& transform)
    {
        return json{
            { "position", Vec3ToJson(transform.position) },
            { "rotation", Vec3ToJson(transform.rotation) },
            { "scale", Vec3ToJson(transform.scale) }
        };
    }

    TransformComponent JsonToTransform(const json& value)
    {
        TransformComponent transform;
        transform.position = JsonToVec3(value.value("position", json::array()), glm::vec3(0.0f));
        transform.rotation = JsonToVec3(value.value("rotation", json::array()), glm::vec3(0.0f));
        transform.scale = JsonToVec3(value.value("scale", json::array()), glm::vec3(1.0f));
        return transform;
    }

    json CameraToJson(const CameraComponent& camera)
    {
        return json{
            { "fov", camera.fov },
            { "nearZ", camera.nearZ },
            { "farZ", camera.farZ },
            { "aspectRatio", camera.aspectRatio }
        };
    }

    CameraComponent JsonToCamera(const json& value)
    {
        CameraComponent camera;
        camera.fov = value.value("fov", 1.8f);
        camera.nearZ = value.value("nearZ", 0.1f);
        camera.farZ = value.value("farZ", 1500.0f);
        camera.aspectRatio = value.value("aspectRatio", 4.0f / 3.0f);
        return camera;
    }
}

void SceneSerializer::Save(const World& world, const std::string& path)
{
    json scene;
    scene["version"] = 1;
    scene["entities"] = json::array();

    auto view = world.registry.view<TagComponent, TransformComponent>();
    for (entt::entity entity : view)
    {
        const auto& tag = view.get<TagComponent>(entity);
        const auto& transform = view.get<TransformComponent>(entity);

        json serializedEntity;
        serializedEntity["tag"] = tag.tag;
        serializedEntity["transform"] = TransformToJson(transform);

        if (const auto* mesh = world.registry.try_get<MeshComponent>(entity))
        {
            serializedEntity["mesh"] = {
                { "id", mesh->meshID }
            };
        }

        if (const auto* camera = world.registry.try_get<CameraComponent>(entity))
        {
            serializedEntity["camera"] = CameraToJson(*camera);
        }

        scene["entities"].push_back(serializedEntity);
    }

    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("Failed to open scene file for writing: " + path);

    out << scene.dump(4);
}

void SceneSerializer::Load(World& world, const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Failed to open scene file for reading: " + path);

    json scene;
    in >> scene;

    world.registry.clear();

    for (const json& serializedEntity : scene.value("entities", json::array()))
    {
        entt::entity entity = world.registry.create();

        world.registry.emplace<TagComponent>(
            entity,
            TagComponent{ serializedEntity.value("tag", "Entity") });

        world.registry.emplace<TransformComponent>(
            entity,
            JsonToTransform(serializedEntity.value("transform", json::object())));

        if (serializedEntity.contains("mesh"))
        {
            const json& mesh = serializedEntity.at("mesh");
            world.registry.emplace<MeshComponent>(
                entity,
                MeshComponent{ mesh.value("id", 0u) });
        }

        if (serializedEntity.contains("camera"))
        {
            world.registry.emplace<CameraComponent>(
                entity,
                JsonToCamera(serializedEntity.at("camera")));
        }
    }
}
