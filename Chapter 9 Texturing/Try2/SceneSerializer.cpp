#include "SceneSerializer.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "Commons.h"
#include "ResourceManager.h"

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

    glm::vec3 SafeNormalize(const glm::vec3& value, const glm::vec3& fallback)
    {
        const float length = glm::length(value);
        if (length <= 0.0001f)
            return fallback;

        return value / length;
    }

    std::wstring StringToWide(const std::string& value)
    {
        return std::wstring(value.begin(), value.end());
    }

    MaterialID JsonToMaterial(ResourceManager& resources, const json& value, const std::string& fallbackName)
    {
        Material material;
        material.name = value.value("name", fallbackName);
        material.color = JsonToVec3(value.value("color", json::array()), glm::vec3(1.0f));
        material.roughness = value.value("roughness", 0.5f);

        if (value.contains("albedo"))
        {
            material.albedo = resources.LoadTexture(StringToWide(value.at("albedo").get<std::string>()));
        }

        if (value.contains("normal"))
        {
            material.normal = resources.LoadTexture(StringToWide(value.at("normal").get<std::string>()));
        }

        return resources.CreateMaterial(material);
    }

    MeshID JsonToMesh(ResourceManager& resources, const json& value, const std::string& entityName)
    {
        if (value.contains("id"))
        {
            return value.value("id", 0u);
        }

        const std::string source = value.value("source", "primitive");
        if (source == "model")
        {
            return resources.LoadMesh(value.at("path").get<std::string>());
        }

        if (source != "primitive")
        {
            throw std::runtime_error("Unknown mesh source: " + source);
        }

        const json materialJson = value.value("material", json::object());
        MaterialID material = JsonToMaterial(resources, materialJson, entityName + "Material");
        const std::string primitive = value.value("primitive", "cube");

        if (primitive == "plane")
        {
            return resources.CreatePlane(material);
        }

        if (primitive == "cube")
        {
            return resources.CreateCube(material);
        }

        if (primitive == "sphere")
        {
            return resources.CreateSphere(material);
        }

        throw std::runtime_error("Unknown primitive type: " + primitive);
    }

    const char* RigidbodyTypeToString(RigidbodyType type)
    {
        switch (type)
        {
        case RigidbodyType::Static:
            return "static";
        case RigidbodyType::Dynamic:
            return "dynamic";
        case RigidbodyType::Kinematic:
            return "kinematic";
        default:
            return "dynamic";
        }
    }

    RigidbodyType JsonToRigidbodyType(const json& value)
    {
        const std::string type = value.get<std::string>();
        if (type == "static") return RigidbodyType::Static;
        if (type == "kinematic") return RigidbodyType::Kinematic;
        return RigidbodyType::Dynamic;
    }

    const char* ColliderTypeToString(ColliderType type)
    {
        switch (type)
        {
        case ColliderType::AABB:
            return "aabb";
        case ColliderType::Sphere:
            return "sphere";
        default:
            return "aabb";
        }
    }

    ColliderType JsonToColliderType(const json& value)
    {
        const std::string type = value.get<std::string>();
        if (type == "sphere") return ColliderType::Sphere;
        return ColliderType::AABB;
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

    json DirectionalLightToJson(const DirectionalLightComponent& light)
    {
        return json{
            { "color", Vec3ToJson(light.color) },
            { "intensity", light.intensity },
            { "direction", Vec3ToJson(light.direction) },
            { "enabled", light.enabled }
        };
    }

    DirectionalLightComponent JsonToDirectionalLight(const json& value)
    {
        DirectionalLightComponent light;
        light.color = JsonToVec3(value.value("color", json::array()), glm::vec3(1.0f));
        light.intensity = value.value("intensity", 1.0f);
        light.direction = SafeNormalize(
            JsonToVec3(value.value("direction", json::array()), glm::vec3(-0.6f, -0.7f, 0.2f)),
            glm::normalize(glm::vec3(-0.6f, -0.7f, 0.2f)));
        light.enabled = value.value("enabled", true);
        return light;
    }

    json PointLightToJson(const PointLightComponent& light)
    {
        return json{
            { "color", Vec3ToJson(light.color) },
            { "intensity", light.intensity },
            { "falloffStart", light.falloffStart },
            { "falloffEnd", light.falloffEnd },
            { "enabled", light.enabled }
        };
    }

    PointLightComponent JsonToPointLight(const json& value)
    {
        PointLightComponent light;
        light.color = JsonToVec3(value.value("color", json::array()), glm::vec3(1.0f));
        light.intensity = value.value("intensity", 1.0f);
        light.falloffStart = value.value("falloffStart", 1.0f);
        light.falloffEnd = value.value("falloffEnd", 25.0f);
        light.enabled = value.value("enabled", true);
        return light;
    }

    json SpotLightToJson(const SpotLightComponent& light)
    {
        return json{
            { "color", Vec3ToJson(light.color) },
            { "intensity", light.intensity },
            { "direction", Vec3ToJson(light.direction) },
            { "falloffStart", light.falloffStart },
            { "falloffEnd", light.falloffEnd },
            { "spotPower", light.spotPower },
            { "enabled", light.enabled }
        };
    }

    SpotLightComponent JsonToSpotLight(const json& value)
    {
        SpotLightComponent light;
        light.color = JsonToVec3(value.value("color", json::array()), glm::vec3(1.0f));
        light.intensity = value.value("intensity", 1.0f);
        light.direction = SafeNormalize(
            JsonToVec3(value.value("direction", json::array()), glm::vec3(0.0f, -1.0f, 0.0f)),
            glm::vec3(0.0f, -1.0f, 0.0f));
        light.falloffStart = value.value("falloffStart", 1.0f);
        light.falloffEnd = value.value("falloffEnd", 35.0f);
        light.spotPower = value.value("spotPower", 32.0f);
        light.enabled = value.value("enabled", true);
        return light;
    }

    json RigidbodyToJson(const RigidbodyComponent& body)
    {
        return json{
            { "type", RigidbodyTypeToString(body.type) },
            { "velocity", Vec3ToJson(body.velocity) },
            { "acceleration", Vec3ToJson(body.acceleration) },
            { "mass", body.mass },
            { "useGravity", body.useGravity }
        };
    }

    RigidbodyComponent JsonToRigidbody(const json& value)
    {
        RigidbodyComponent body;
        body.type = JsonToRigidbodyType(value.value("type", json("dynamic")));
        body.velocity = JsonToVec3(value.value("velocity", json::array()), glm::vec3(0.0f));
        body.acceleration = JsonToVec3(value.value("acceleration", json::array()), glm::vec3(0.0f));
        body.mass = value.value("mass", 1.0f);
        body.useGravity = value.value("useGravity", true);
        return body;
    }

    json ColliderToJson(const ColliderComponent& collider)
    {
        return json{
            { "type", ColliderTypeToString(collider.type) },
            { "offset", Vec3ToJson(collider.offset) },
            { "halfExtents", Vec3ToJson(collider.halfExtents) },
            { "radius", collider.radius },
            { "isTrigger", collider.isTrigger },
            { "restitution", collider.restitution },
            { "friction", collider.friction }
        };
    }

    ColliderComponent JsonToCollider(const json& value)
    {
        ColliderComponent collider;
        collider.type = JsonToColliderType(value.value("type", json("aabb")));
        collider.offset = JsonToVec3(value.value("offset", json::array()), glm::vec3(0.0f));
        collider.halfExtents = JsonToVec3(value.value("halfExtents", json::array()), glm::vec3(0.5f));
        collider.radius = value.value("radius", 0.5f);
        collider.isTrigger = value.value("isTrigger", false);
        collider.restitution = value.value("restitution", 0.0f);
        collider.friction = value.value("friction", 0.5f);
        return collider;
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

        if (const auto* light = world.registry.try_get<DirectionalLightComponent>(entity))
        {
            serializedEntity["directionalLight"] = DirectionalLightToJson(*light);
        }

        if (const auto* light = world.registry.try_get<PointLightComponent>(entity))
        {
            serializedEntity["pointLight"] = PointLightToJson(*light);
        }

        if (const auto* light = world.registry.try_get<SpotLightComponent>(entity))
        {
            serializedEntity["spotLight"] = SpotLightToJson(*light);
        }

        if (const auto* body = world.registry.try_get<RigidbodyComponent>(entity))
        {
            serializedEntity["rigidbody"] = RigidbodyToJson(*body);
        }

        if (const auto* collider = world.registry.try_get<ColliderComponent>(entity))
        {
            serializedEntity["collider"] = ColliderToJson(*collider);
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
    ResourceManager resources;
    Load(world, resources, path);
}

void SceneSerializer::Load(World& world, ResourceManager& resources, const std::string& path)
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
        const std::string tag = serializedEntity.value("tag", "Entity");

        world.registry.emplace<TagComponent>(
            entity,
            TagComponent{ tag });

        world.registry.emplace<TransformComponent>(
            entity,
            JsonToTransform(serializedEntity.value("transform", json::object())));

        if (serializedEntity.contains("mesh"))
        {
            const json& mesh = serializedEntity.at("mesh");
            world.registry.emplace<MeshComponent>(
                entity,
                MeshComponent{ JsonToMesh(resources, mesh, tag) });
        }

        if (serializedEntity.contains("camera"))
        {
            world.registry.emplace<CameraComponent>(
                entity,
                JsonToCamera(serializedEntity.at("camera")));
        }

        if (serializedEntity.contains("directionalLight"))
        {
            world.registry.emplace<DirectionalLightComponent>(
                entity,
                JsonToDirectionalLight(serializedEntity.at("directionalLight")));
        }

        if (serializedEntity.contains("pointLight"))
        {
            world.registry.emplace<PointLightComponent>(
                entity,
                JsonToPointLight(serializedEntity.at("pointLight")));
        }

        if (serializedEntity.contains("spotLight"))
        {
            world.registry.emplace<SpotLightComponent>(
                entity,
                JsonToSpotLight(serializedEntity.at("spotLight")));
        }

        if (serializedEntity.contains("rigidbody"))
        {
            world.registry.emplace<RigidbodyComponent>(
                entity,
                JsonToRigidbody(serializedEntity.at("rigidbody")));
        }

        if (serializedEntity.contains("collider"))
        {
            world.registry.emplace<ColliderComponent>(
                entity,
                JsonToCollider(serializedEntity.at("collider")));
        }
    }
}
