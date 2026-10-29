#include "SceneSerializer.h"
#include "AssetPaths.h"
#include <cmath>

#include <fstream>
#include <filesystem>
#include <algorithm>
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
        return std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(value.data()), value.size())).wstring();
    }

    MaterialDesc ReadMaterial(const json& value, const std::string& fallbackName)
    {
        MaterialDesc material;
        material.name = value.value("name", fallbackName);
        material.color = JsonToVec3(value.value("color", json::array()), glm::vec3(1.0f));
        material.roughness = value.value("roughness", 0.5f);
        if (value.contains("albedo")) material.albedoTexture = StringToWide(value.at("albedo").get<std::string>());
        if (value.contains("normal")) material.normalTexture = StringToWide(value.at("normal").get<std::string>());
        return material;
    }

    SceneSerializer::MeshDescription ReadMesh(const json& value, const std::string& entityName)
    {
        SceneSerializer::MeshDescription mesh;
        // A Play snapshot stores both: the session id for reuse and the source for later saves.
        if (value.contains("id")) mesh.existing = value.at("id").get<MeshID>();
        if (mesh.existing && !value.contains("source")) return mesh;
        mesh.source = value.value("source", "primitive");
        if (mesh.source == "model")
        {
            mesh.path = value.at("path").get<std::string>();
            if (value.contains("material")) mesh.material = ReadMaterial(value.at("material"), entityName + "Material");
        }
        else if (mesh.source == "primitive")
        {
            mesh.primitive = value.value("primitive", "cube");
            if (mesh.primitive != "plane" && mesh.primitive != "cube" && mesh.primitive != "sphere")
                throw std::runtime_error("Unknown primitive type: " + mesh.primitive);
            mesh.material = ReadMaterial(value.value("material", json::object()), entityName + "Material");
        }
        else throw std::runtime_error("Unknown mesh source: " + mesh.source);
        return mesh;
    }

    json MaterialToJson(const MaterialDesc& material)
    {
        json value{ { "name", material.name }, { "color", Vec3ToJson(material.color) }, { "roughness", material.roughness } };
        if (!material.albedoTexture.empty()) value["albedo"] = AssetPaths::ToUtf8(material.albedoTexture);
        if (!material.normalTexture.empty()) value["normal"] = AssetPaths::ToUtf8(material.normalTexture);
        return value;
    }

    json MeshToJson(const SceneSerializer::MeshDescription& mesh)
    {
        json value{ { "source", mesh.source } };
        if (mesh.source == "model") value["path"] = mesh.path;
        else value["primitive"] = mesh.primitive;
        if (mesh.material) value["material"] = MaterialToJson(*mesh.material);
        return value;
    }

    MeshID CreateMesh(ResourceManager& resources, const SceneSerializer::MeshDescription& mesh)
    {
        if (mesh.existing) return *mesh.existing;
        if (mesh.source == "model")
        {
            const MeshID id = resources.LoadMesh(mesh.path);
            if (mesh.material) resources.SetMeshMaterial(id, resources.CreateTexturedMaterial(*mesh.material));
            return id;
        }
        const MaterialID material = resources.CreateTexturedMaterial(*mesh.material);
        if (mesh.primitive == "plane") return resources.CreatePlane(material);
        if (mesh.primitive == "sphere") return resources.CreateSphere(material);
        return resources.CreateCube(material);
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

namespace
{
    json ScriptToJson(const ScriptComponent& script, const std::filesystem::path& root)
    {
        json parameters = json::object();
        for (const auto& [name, value] : script.parameters)
        {
            json item;
            item["type"] = ScriptValueType(value);
            std::visit([&](const auto& v)
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, glm::vec3>) item["value"] = Vec3ToJson(v);
                else item["value"] = v;
            }, value);
            parameters[name] = std::move(item);
        }
        return { { "path", AssetPaths::ScriptPath(root, script.path) },
            { "enabled", script.enabled }, { "parameters", std::move(parameters) } };
    }
    ScriptComponent ScriptFromJson(const json& value)
    {
        if (!value.is_object()) throw std::runtime_error("Script must be an object");
        ScriptComponent result;
        result.path = value.at("path").get<std::string>();
        const auto path = AssetPaths::FromUtf8(result.path).lexically_normal();
        if (result.path.empty() || result.path.size() > 4096 || result.path.find('\0') != std::string::npos ||
            path.is_absolute() || path.begin() == path.end() || *path.begin() != "Scripts")
            throw std::runtime_error("Script path must be relative to the asset root, inside Scripts/");
        result.path = AssetPaths::ToUtf8(path);
        result.enabled = value.value("enabled", true);
        const auto parameters = value.value("parameters", json::object());
        if (!parameters.is_object() || parameters.size() > 64) throw std::runtime_error("Script parameters must contain at most 64 named values");
        for (auto it = parameters.begin(); it != parameters.end(); ++it)
        {
            if (it.key().empty() || it.key().size() > 128 || it.key().find('\0') != std::string::npos)
                throw std::runtime_error("Invalid script parameter name");
            const auto type = it.value().at("type").get<std::string>();
            const auto& data = it.value().at("value");
            ScriptValue parameter;
            if (type == "bool" && data.is_boolean()) parameter = data.get<bool>();
            else if (type == "number" && data.is_number() && std::isfinite(data.get<double>())) parameter = data.get<double>();
            else if (type == "string" && data.is_string() && data.get_ref<const std::string&>().size() <= 4096) parameter = data.get<std::string>();
            else if (type == "Vec3" && data.is_array() && data.size() == 3)
            {
                glm::vec3 vector;
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!data[axis].is_number() || !std::isfinite(data[axis].get<float>()))
                        throw std::runtime_error("Script Vec3 must contain finite numbers");
                    vector[axis] = data[axis].get<float>();
                }
                parameter = vector;
            }
            else throw std::runtime_error("Unsupported or incompatible script parameter: " + it.key());
            result.parameters.emplace(it.key(), std::move(parameter));
        }
        return result;
    }
    SceneSerializer::EntityDescription ReadEntity(const json& value, const std::string& defaultTag)
    {
        if (!value.is_object()) throw std::runtime_error("Entity must be an object");
        SceneSerializer::EntityDescription entity;
        entity.tag = value.value("tag", defaultTag);
        entity.transform = JsonToTransform(value.value("transform", json::object()));
        if (value.contains("mesh")) entity.mesh = ReadMesh(value.at("mesh"), entity.tag);
        if (value.contains("camera")) entity.camera = JsonToCamera(value.at("camera"));
        if (value.contains("directionalLight")) entity.directionalLight = JsonToDirectionalLight(value.at("directionalLight"));
        if (value.contains("pointLight")) entity.pointLight = JsonToPointLight(value.at("pointLight"));
        if (value.contains("spotLight")) entity.spotLight = JsonToSpotLight(value.at("spotLight"));
        if (value.contains("rigidbody")) entity.rigidbody = JsonToRigidbody(value.at("rigidbody"));
        if (value.contains("collider")) entity.collider = JsonToCollider(value.at("collider"));
        if (value.contains("script")) entity.script = ScriptFromJson(value.at("script"));
        return entity;
    }
}

size_t SceneSerializer::Save(const World& world, const std::string& path, const std::filesystem::path& assetRoot,
    bool keepRuntimeMeshIds)
{
    json scene;
    scene["version"] = 1;
    scene["name"] = AssetPaths::ToUtf8(AssetPaths::FromUtf8(path).stem());
    scene["entities"] = json::array();
    size_t runtimeOnlyMeshes = 0;

    auto view = world.registry.view<TagComponent, TransformComponent>();
    // EnTT iterates newest first; write entities in creation order so load/save keeps the file order.
    std::vector<entt::entity> entities(view.begin(), view.end());
    std::reverse(entities.begin(), entities.end());
    for (entt::entity entity : entities)
    {
        const auto& tag = view.get<TagComponent>(entity);
        const auto& transform = view.get<TransformComponent>(entity);

        json serializedEntity;
        serializedEntity["tag"] = tag.tag;
        serializedEntity["transform"] = TransformToJson(transform);

        if (const auto* mesh = world.registry.try_get<MeshComponent>(entity))
        {
            const auto* source = world.registry.try_get<MeshSourceComponent>(entity);
            json value = source ? MeshToJson(source->mesh) : json::object();
            if (!source) ++runtimeOnlyMeshes;
            if (!source || keepRuntimeMeshIds) value["id"] = mesh->meshID;
            serializedEntity["mesh"] = std::move(value);
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
        if (const auto* script = world.registry.try_get<ScriptComponent>(entity))
            serializedEntity["script"] = ScriptToJson(*script, assetRoot.empty() ? AssetPaths::FindRoot() : assetRoot);

        scene["entities"].push_back(serializedEntity);
    }

    std::ofstream out(AssetPaths::FromUtf8(path));
    if (!out)
        throw std::runtime_error("Failed to open scene file for writing: " + path);

    out << scene.dump(2) << '\n';
    return runtimeOnlyMeshes;
}

void SceneSerializer::Load(World& world, const std::string& path)
{
    ResourceManager resources;
    Load(world, resources, path);
}

SceneSerializer::SceneData SceneSerializer::Read(const std::string& path)
{
    ZoneScopedN("Scene CPU read parse and validate");
    std::ifstream in(std::filesystem::path(StringToWide(path)));
    if (!in) throw std::runtime_error("Failed to open scene file for reading: " + path);
    json scene;
    in >> scene;
    SceneData data;
    // Use a reference: value("entities", ...) copied the entire JSON tree.
    if (!scene.contains("entities")) return data;
    const auto& entities = scene.at("entities");
    if (!entities.is_array()) throw std::runtime_error("Scene entities must be an array");
    data.entities.reserve(entities.size());
    for (const auto& value : entities) data.entities.push_back(ReadEntity(value, "Entity"));
    // A scene can put its camera last. Publish camera and lights before mesh
    // batches, so progressive rendering has a valid view from the first batch.
    const auto priority = [](const EntityDescription& entity)
    { return entity.camera ? 0 : (entity.directionalLight || entity.pointLight || entity.spotLight ? 1 : 2); };
    std::stable_sort(data.entities.begin(), data.entities.end(), [&](const auto& a, const auto& b)
        { return priority(a) < priority(b); });
    return data; // The large JSON tree is destroyed on the reading worker.
}

SceneSerializer::EntityDescription SceneSerializer::ReadPrefab(const std::filesystem::path& file, const std::string& defaultTag)
{
    std::ifstream in(file);
    if (!in) throw std::runtime_error("Failed to open prefab: " + AssetPaths::ToUtf8(file));
    if (std::filesystem::file_size(file) > 1024 * 1024) throw std::runtime_error("Prefab exceeds 1 MiB");
    json value;
    try { in >> value; }
    catch (const json::exception& error) { throw std::runtime_error("Prefab JSON error: " + std::string(error.what())); }
    // Mesh IDs are runtime handles of one session; a template must describe its mesh.
    if (value.is_object() && value.contains("mesh") && value.at("mesh").contains("id"))
        throw std::runtime_error("Prefab mesh must use source/primitive or a model path, not a runtime id");
    if (value.is_object() && value.contains("camera")) throw std::runtime_error("Prefabs cannot contain a camera");
    return ReadEntity(value, defaultTag);
}

void SceneSerializer::Instantiate(entt::registry& registry, entt::entity entity, ResourceManager& resources,
    const EntityDescription& description)
{
    registry.emplace<TagComponent>(entity, TagComponent{ description.tag });
    registry.emplace<TransformComponent>(entity, description.transform);
    if (description.mesh)
    {
        registry.emplace<MeshComponent>(entity, MeshComponent{ CreateMesh(resources, *description.mesh) });
        if (!description.mesh->source.empty())
        {
            auto source = *description.mesh;
            source.existing.reset();
            registry.emplace<MeshSourceComponent>(entity, MeshSourceComponent{ std::move(source) });
        }
    }
    if (description.camera) registry.emplace<CameraComponent>(entity, *description.camera);
    if (description.directionalLight) registry.emplace<DirectionalLightComponent>(entity, *description.directionalLight);
    if (description.pointLight) registry.emplace<PointLightComponent>(entity, *description.pointLight);
    if (description.spotLight) registry.emplace<SpotLightComponent>(entity, *description.spotLight);
    if (description.rigidbody) registry.emplace<RigidbodyComponent>(entity, *description.rigidbody);
    if (description.collider) registry.emplace<ColliderComponent>(entity, *description.collider);
    if (description.script) registry.emplace<ScriptComponent>(entity, *description.script);
}

void SceneSerializer::CreateEntity(World& world, ResourceManager& resources, const EntityDescription& description)
{
    const entt::entity entity = world.registry.create();
    try { Instantiate(world.registry, entity, resources, description); }
    catch (...) { world.registry.destroy(entity); throw; }
}

void SceneSerializer::Load(World& world, ResourceManager& resources, const std::string& path)
{
    auto data = Read(path);
    world.registry.clear();
    for (const auto& entity : data.entities) CreateEntity(world, resources, entity);
}
