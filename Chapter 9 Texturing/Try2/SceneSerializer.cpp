#include "SceneSerializer.h"
#include "AssetPaths.h"
#include <cmath>

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <array>
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

    MaterialRenderState ReadRenderState(const json& value)
    {
        MaterialRenderState state;
        const auto read = [&](const char* field, auto& destination, const auto& names)
        {
            if (!value.contains(field))
            {
                return;
            }
            const std::string path = std::string("surface.renderState.") + field;
            if (!value.at(field).is_string())
            {
                throw std::runtime_error(path + ": expected an enum name");
            }
            const auto name = value.at(field).get<std::string>();
            const auto found = std::find(names.begin(), names.end(), name);
            if (found == names.end())
            {
                throw std::runtime_error(path + ": unknown value '" + name + "'");
            }
            destination = static_cast<std::remove_reference_t<decltype(destination)>>(found - names.begin());
        };
        if (!value.is_object())
        {
            throw std::runtime_error("surface.renderState: expected an object");
        }
        const std::array fields = {"depthTest", "depthWrite", "depthCompare", "blendEnabled",
            "sourceColor", "destinationColor", "colorOperation", "sourceAlpha", "destinationAlpha", "alphaOperation"};
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
        {
            if (std::find(fields.begin(), fields.end(), iterator.key()) == fields.end())
            {
                throw std::runtime_error("surface.renderState." + iterator.key() + ": unknown field");
            }
        }
        const auto readBool = [&](const char* field, bool& destination)
        {
            if (value.contains(field))
            {
                if (!value.at(field).is_boolean())
                {
                    throw std::runtime_error(std::string("surface.renderState.") + field + ": expected a boolean");
                }
                destination = value.at(field).get<bool>();
            }
        };
        readBool("depthTest", state.depthTest);
        readBool("depthWrite", state.depthWrite);
        readBool("blendEnabled", state.blendEnabled);
        read("depthCompare", state.depthCompare, DepthComparisonNames);
        read("sourceColor", state.sourceColor, BlendFactorNames);
        read("destinationColor", state.destinationColor, BlendFactorNames);
        read("colorOperation", state.colorOperation, BlendOperationNames);
        read("sourceAlpha", state.sourceAlpha, BlendFactorNames);
        read("destinationAlpha", state.destinationAlpha, BlendFactorNames);
        read("alphaOperation", state.alphaOperation, BlendOperationNames);
        state.Validate();
        return state;
    }

    json RenderStateToJson(const MaterialRenderState& state)
    {
        state.Validate();
        return {
            {"depthTest", state.depthTest},
            {"depthWrite", state.depthWrite},
            {"depthCompare", DepthComparisonNames[static_cast<size_t>(state.depthCompare)]},
            {"blendEnabled", state.blendEnabled},
            {"sourceColor", BlendFactorNames[static_cast<size_t>(state.sourceColor)]},
            {"destinationColor", BlendFactorNames[static_cast<size_t>(state.destinationColor)]},
            {"colorOperation", BlendOperationNames[static_cast<size_t>(state.colorOperation)]},
            {"sourceAlpha", BlendFactorNames[static_cast<size_t>(state.sourceAlpha)]},
            {"destinationAlpha", BlendFactorNames[static_cast<size_t>(state.destinationAlpha)]},
            {"alphaOperation", BlendOperationNames[static_cast<size_t>(state.alphaOperation)]}
        };
    }

    constexpr std::array MaterialBaseOnlyFields = {"shader", "vertexEntry", "pixelEntry", "editorVariant",
        "maskedVariant", "mode", "twoSided", "renderState"};

    float ReadOverrideNumber(const json& value, const std::string& field)
    {
        if (!value.is_number())
        {
            throw std::runtime_error("overrides." + field + ": expected a number");
        }
        const float number = value.get<float>();
        if (!std::isfinite(number))
        {
            throw std::runtime_error("overrides." + field + ": expected a finite number");
        }
        return number;
    }

    template <size_t ComponentCount>
    glm::vec<ComponentCount, float> ReadOverrideVector(const json& value, const std::string& field)
    {
        if (!value.is_array() || value.size() != ComponentCount)
        {
            throw std::runtime_error("overrides." + field + ": expected an array of " +
                std::to_string(ComponentCount) + " numbers");
        }
        glm::vec<ComponentCount, float> result(0.0f);
        for (size_t component = 0; component < ComponentCount; ++component)
        {
            result[static_cast<glm::length_t>(component)] = ReadOverrideNumber(value.at(component), field);
        }
        return result;
    }

    template <typename Value>
    struct OverrideField
    {
        const char* name;
        std::optional<Value> MaterialOverrides::* member;
    };

    constexpr std::array ScalarOverrideFields = {
        OverrideField<float>{"alpha", &MaterialOverrides::alpha},
        OverrideField<float>{"roughness", &MaterialOverrides::roughness},
        OverrideField<float>{"metallic", &MaterialOverrides::metallic},
        OverrideField<float>{"normalStrength", &MaterialOverrides::normalStrength},
        OverrideField<float>{"aoStrength", &MaterialOverrides::aoStrength},
        OverrideField<float>{"alphaCutoff", &MaterialOverrides::alphaCutoff},
        OverrideField<float>{"emissiveIntensity", &MaterialOverrides::emissiveIntensity},
        OverrideField<float>{"emissiveLightRange", &MaterialOverrides::emissiveLightRange}};
    constexpr std::array ColorOverrideFields = {
        OverrideField<glm::vec3>{"color", &MaterialOverrides::color},
        OverrideField<glm::vec3>{"emissiveColor", &MaterialOverrides::emissiveColor}};
    constexpr std::array UVOverrideFields = {
        OverrideField<glm::vec2>{"uvScale", &MaterialOverrides::uvScale},
        OverrideField<glm::vec2>{"uvOffset", &MaterialOverrides::uvOffset}};
    constexpr std::array MapOverrideFields = {
        OverrideField<std::wstring>{"albedo", &MaterialOverrides::albedoTexture},
        OverrideField<std::wstring>{"normal", &MaterialOverrides::normalTexture},
        OverrideField<std::wstring>{"orm", &MaterialOverrides::ormTexture},
        OverrideField<std::wstring>{"emissive", &MaterialOverrides::emissiveTexture}};

    template <typename Field, size_t FieldCount, typename Reader>
    bool ReadOverrideField(const std::array<Field, FieldCount>& fields, const std::string& name,
        MaterialOverrides& overrides, Reader&& read)
    {
        for (const auto& field : fields)
        {
            if (name == field.name)
            {
                overrides.*(field.member) = read();
                return true;
            }
        }
        return false;
    }

    MaterialOverrides ReadMaterialOverrides(const json& value)
    {
        if (!value.is_object())
        {
            throw std::runtime_error("overrides: expected an object");
        }
        MaterialOverrides overrides;
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
        {
            const std::string& field = iterator.key();
            const json& entry = iterator.value();
            const auto readNumber = [&]()
            {
                return ReadOverrideNumber(entry, field);
            };
            const auto readColor = [&]()
            {
                return ReadOverrideVector<3>(entry, field);
            };
            const auto readUV = [&]()
            {
                return ReadOverrideVector<2>(entry, field);
            };
            const auto readMap = [&]()
            {
                if (!entry.is_string())
                {
                    throw std::runtime_error("overrides." + field + ": expected a texture path or \"\" to clear it");
                }
                return StringToWide(entry.get<std::string>());
            };
            const bool known = ReadOverrideField(ScalarOverrideFields, field, overrides, readNumber) ||
                ReadOverrideField(ColorOverrideFields, field, overrides, readColor) ||
                ReadOverrideField(UVOverrideFields, field, overrides, readUV) ||
                ReadOverrideField(MapOverrideFields, field, overrides, readMap);
            if (known)
            {
                continue;
            }
            if (std::find(MaterialBaseOnlyFields.begin(), MaterialBaseOnlyFields.end(), field) != MaterialBaseOnlyFields.end())
            {
                throw std::runtime_error("overrides." + field +
                    ": belongs to the base material (shader, mode and render state are shared by all instances)");
            }
            throw std::runtime_error("overrides." + field + ": unknown parameter");
        }
        return overrides;
    }

    MaterialDesc ReadMaterialInstance(const json& value, const std::string& fallbackName)
    {
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
        {
            const std::string& field = iterator.key();
            if (field != "name" && field != "parent" && field != "overrides")
            {
                throw std::runtime_error(field + ": a material instance only sets 'name', 'parent' and 'overrides'");
            }
        }
        if (!value.at("parent").is_string() || value.at("parent").get<std::string>().empty())
        {
            throw std::runtime_error("parent: expected a .lsmaterial path relative to the project");
        }
        MaterialDesc material;
        material.name = value.value("name", fallbackName);
        material.parent = value.at("parent").get<std::string>();
        if (value.contains("overrides"))
        {
            material.overrides = ReadMaterialOverrides(value.at("overrides"));
        }
        return material;
    }

    json MaterialOverridesToJson(const MaterialOverrides& overrides, const std::filesystem::path& root)
    {
        json value = json::object();
        for (const auto& field : ScalarOverrideFields)
        {
            if (const auto& number = overrides.*(field.member))
            {
                value[field.name] = *number;
            }
        }
        for (const auto& field : ColorOverrideFields)
        {
            if (const auto& color = overrides.*(field.member))
            {
                value[field.name] = Vec3ToJson(*color);
            }
        }
        for (const auto& field : UVOverrideFields)
        {
            if (const auto& uv = overrides.*(field.member))
            {
                value[field.name] = {uv->x, uv->y};
            }
        }
        for (const auto& field : MapOverrideFields)
        {
            if (const auto& path = overrides.*(field.member))
            {
                value[field.name] = path->empty() ? std::string() :
                    AssetPaths::MakeProjectRelative(root, AssetPaths::ToUtf8(*path));
            }
        }
        return value;
    }

    MaterialDesc ReadMaterial(const json& value, const std::string& fallbackName)
    {
        if (value.is_object() && value.contains("parent"))
        {
            return ReadMaterialInstance(value, fallbackName);
        }
        if (value.is_object() && value.contains("overrides"))
        {
            throw std::runtime_error("overrides: only a material instance with a 'parent' can override parameters");
        }
        MaterialDesc material;
        material.name = value.value("name", fallbackName);
        material.color = JsonToVec3(value.value("color", json::array()), glm::vec3(1.0f));
        material.roughness = value.value("roughness", 0.5f);
        if (value.contains("albedo")) material.albedoTexture = StringToWide(value.at("albedo").get<std::string>());
        if (value.contains("normal")) material.normalTexture = StringToWide(value.at("normal").get<std::string>());
        if (value.contains("surface"))
        {
            const auto& description = value.at("surface");
            MaterialBase& base = material.surface.emplace();
            auto& parameters = base.defaults;
            const std::string mode = description.value("mode", "opaque");
            if (mode == "masked")
            {
                base.mode = SurfaceMode::Masked;
            }
            else if (mode != "opaque")
            {
                throw std::runtime_error("Unsupported surface mode: " + mode);
            }
            base.twoSided = description.value("twoSided", false);
            if (description.contains("renderState"))
            {
                base.renderState = ReadRenderState(description.at("renderState"));
            }
            if (description.contains("shader"))
            {
                base.program.file = StringToWide(description.at("shader").get<std::string>());
            }
            base.program.vertexEntry = description.value("vertexEntry", base.program.vertexEntry);
            base.program.pixelEntry = description.value("pixelEntry", base.program.pixelEntry);
            base.program.editorVariant = description.value("editorVariant", true);
            base.program.maskedVariant = description.value("maskedVariant", true);
            parameters.baseColor = glm::vec4(material.color, description.value("alpha", 1.0f));
            parameters.roughness = material.roughness;
            parameters.normalStrength = description.value("normalStrength", 1.0f);
            parameters.aoStrength = description.value("aoStrength", 1.0f);
            parameters.alphaCutoff = description.value("alphaCutoff", 0.5f);
            parameters.emissiveColor = JsonToVec3(description.value("emissiveColor", json::array()));
            parameters.emissiveIntensity = description.value("emissiveIntensity", 1.0f);
            parameters.emissiveLightRange = description.value("emissiveLightRange", 5.0f);
            if (description.contains("metallic"))
            {
                parameters.metallic = description.at("metallic").get<float>();
            }
            const auto readUV = [&](const char* name, glm::vec2 fallback)
            {
                if (!description.contains(name))
                {
                    return fallback;
                }
                const auto& array = description.at(name);
                return glm::vec2(array.at(0).get<float>(), array.at(1).get<float>());
            };
            parameters.uvScale = readUV("uvScale", glm::vec2(1));
            parameters.uvOffset = readUV("uvOffset", glm::vec2(0));
            material.ormTexture = StringToWide(description.value("orm", ""));
            material.emissiveTexture = StringToWide(description.value("emissive", ""));
        }
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

    // Files inside the project are written relative to it, so the project folder can move.
    std::string ProjectPath(const std::filesystem::path& root, const std::string& path)
    {
        return AssetPaths::MakeProjectRelative(root, path);
    }

    json MaterialToJson(const MaterialDesc& material, const std::filesystem::path& root)
    {
        if (material.IsInstance())
        {
            return json{
                {"name", material.name},
                {"parent", ProjectPath(root, material.parent)},
                {"overrides", MaterialOverridesToJson(material.overrides, root)}
            };
        }
        json value{ { "name", material.name }, { "color", Vec3ToJson(material.color) }, { "roughness", material.roughness } };
        if (!material.albedoTexture.empty()) value["albedo"] = ProjectPath(root, AssetPaths::ToUtf8(material.albedoTexture));
        if (!material.normalTexture.empty()) value["normal"] = ProjectPath(root, AssetPaths::ToUtf8(material.normalTexture));
        if (material.surface)
        {
            const auto& base = *material.surface;
            const auto& parameters = base.defaults;
            auto& description = value["surface"];
            description = {
                {"shader", AssetPaths::ToUtf8(base.program.file)},
                {"vertexEntry", base.program.vertexEntry},
                {"pixelEntry", base.program.pixelEntry},
                {"editorVariant", base.program.editorVariant},
                {"maskedVariant", base.program.maskedVariant},
                {"mode", base.mode == SurfaceMode::Masked ? "masked" : "opaque"},
                {"twoSided", base.twoSided},
                {"renderState", RenderStateToJson(base.renderState)},
                {"alpha", parameters.baseColor.a},
                {"normalStrength", parameters.normalStrength},
                {"aoStrength", parameters.aoStrength},
                {"alphaCutoff", parameters.alphaCutoff},
                {"emissiveColor", Vec3ToJson(parameters.emissiveColor)},
                {"emissiveIntensity", parameters.emissiveIntensity},
                {"emissiveLightRange", parameters.emissiveLightRange},
                {"uvScale", {parameters.uvScale.x, parameters.uvScale.y}},
                {"uvOffset", {parameters.uvOffset.x, parameters.uvOffset.y}}
            };
            if (parameters.metallic)
            {
                description["metallic"] = *parameters.metallic;
            }
            if (!material.ormTexture.empty())
            {
                description["orm"] = ProjectPath(root, AssetPaths::ToUtf8(material.ormTexture));
            }
            if (!material.emissiveTexture.empty())
            {
                description["emissive"] = ProjectPath(root, AssetPaths::ToUtf8(material.emissiveTexture));
            }
        }
        return value;
    }

    json MeshToJson(const SceneSerializer::MeshDescription& mesh, const std::filesystem::path& root)
    {
        json value{ { "source", mesh.source } };
        if (mesh.source == "model") value["path"] = ProjectPath(root, mesh.path);
        else value["primitive"] = mesh.primitive;
        if (mesh.material) value["material"] = MaterialToJson(*mesh.material, root);
        return value;
    }

    json TerrainToJson(const TerrainComponent& terrain, const std::filesystem::path& root)
    {
        json value = Terrain::ToJson(terrain);
        for (const char* key : { "heightmap", "weightmap" })
            if (value.contains(key) && value[key].is_string()) value[key] = ProjectPath(root, value[key].get<std::string>());
        if (value.contains("layers"))
            for (auto& layer : value["layers"])
                for (const char* key : { "albedo", "normal" })
                    if (layer.contains(key) && layer[key].is_string()) layer[key] = ProjectPath(root, layer[key].get<std::string>());
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
            { "enabled", light.enabled }
        };
    }

    DirectionalLightComponent JsonToDirectionalLight(const json& value)
    {
        DirectionalLightComponent light;
        light.color = JsonToVec3(value.value("color", json::array()), glm::vec3(1.0f));
        light.intensity = value.value("intensity", 1.0f);
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
        result.path = AssetPaths::NormalizeScriptPath(value.at("path").get<std::string>());
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
        if (value.contains("renderOrder"))
        {
            entity.renderOrder = RenderOrderComponent{value.at("renderOrder").get<int>()};
        }
        if (value.contains("materialComponent"))
        {
            entity.materialAsset = value.at("materialComponent").at("asset").get<std::string>();
        }
        if (value.contains("mesh")) entity.mesh = ReadMesh(value.at("mesh"), entity.tag);
        if (value.contains("camera")) entity.camera = JsonToCamera(value.at("camera"));
        if (value.contains("directionalLight")) entity.directionalLight = JsonToDirectionalLight(value.at("directionalLight"));
        // Migrate legacy scenes once; explicit rotation always takes precedence.
        if (entity.directionalLight && value.at("directionalLight").contains("direction") &&
            (!value.contains("transform") || !value.at("transform").contains("rotation")))
        {
            const glm::vec3 direction = SafeNormalize(
                JsonToVec3(value.at("directionalLight").at("direction"), glm::vec3(0, 0, 1)), glm::vec3(0, 0, 1));
            entity.transform.rotation = glm::vec3(std::asin(glm::clamp(-direction.y, -1.0f, 1.0f)),
                std::atan2(direction.x, direction.z), 0);
        }
        if (value.contains("pointLight")) entity.pointLight = JsonToPointLight(value.at("pointLight"));
        if (value.contains("spotLight")) entity.spotLight = JsonToSpotLight(value.at("spotLight"));
        if (entity.spotLight && value.at("spotLight").contains("direction") &&
            (!value.contains("transform") || !value.at("transform").contains("rotation")))
        {
            const glm::vec3 direction = SafeNormalize(
                JsonToVec3(value.at("spotLight").at("direction"), glm::vec3(0, -1, 0)), glm::vec3(0, -1, 0));
            entity.transform.rotation = glm::vec3(std::asin(glm::clamp(-direction.y, -1.0f, 1.0f)),
                std::atan2(direction.x, direction.z), 0);
        }
        if (value.contains("rigidbody")) entity.rigidbody = JsonToRigidbody(value.at("rigidbody"));
        if (value.contains("collider")) entity.collider = JsonToCollider(value.at("collider"));
        if (value.contains("script")) entity.script = ScriptFromJson(value.at("script"));
        if (value.contains("terrain"))
        {
            entity.terrain=Terrain::FromJson(value.at("terrain"));
            if(!Terrain::ValidTransform(entity.transform))throw std::runtime_error("Terrain requires translation and positive scale; rotation is unsupported");
        }
        return entity;
    }
}

size_t SceneSerializer::Save(const World& world, const std::string& path, const std::filesystem::path& assetRoot,
    bool keepRuntimeMeshIds, bool portableOnly)
{
    const auto root = assetRoot.empty() ? AssetPaths::FindRoot() : assetRoot;
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
        if (const auto* order = world.registry.try_get<RenderOrderComponent>(entity))
        {
            serializedEntity["renderOrder"] = order->layer;
        }

        if (const auto* mesh = world.registry.try_get<MeshComponent>(entity))
        {
            const auto* source = world.registry.try_get<MeshSourceComponent>(entity);
            if (!source) ++runtimeOnlyMeshes;
            // Session ids mean nothing in another process: portable files omit such meshes.
            if (source || !portableOnly)
            {
                json value = source ? MeshToJson(source->mesh, root) : json::object();
                if (!source || keepRuntimeMeshIds) value["id"] = mesh->meshID;
                serializedEntity["mesh"] = std::move(value);
            }
        }

        if (const auto* material = world.registry.try_get<MaterialComponent>(entity))
        {
            serializedEntity["materialComponent"] = {{"asset", ProjectPath(root, material->assetPath)}};
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
            serializedEntity["script"] = ScriptToJson(*script, root);
        if(const auto* terrain=world.registry.try_get<TerrainComponent>(entity))serializedEntity["terrain"]=TerrainToJson(*terrain, root);

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
try
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
catch (const std::exception& error)
{
    throw std::runtime_error(path + ": " + error.what());
}

SceneSerializer::EntityDescription SceneSerializer::ReadPrefab(const std::filesystem::path& file, const std::string& defaultTag)
try
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
catch (const std::exception& error)
{
    throw std::runtime_error(AssetPaths::ToUtf8(file) + ": " + error.what());
}

void SceneSerializer::Instantiate(entt::registry& registry, entt::entity entity, ResourceManager& resources,
    const EntityDescription& description)
{
    registry.emplace<TagComponent>(entity, TagComponent{ description.tag });
    registry.emplace<TransformComponent>(entity, description.transform);
    if (description.renderOrder)
    {
        registry.emplace<RenderOrderComponent>(entity, *description.renderOrder);
    }
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
    if (!description.materialAsset.empty())
    {
        MaterialComponent component;
        component.assetPath = description.materialAsset;
        try
        {
            component.material = resources.LoadMaterialAsset(component.assetPath);
        }
        catch (const std::exception& error)
        {
            component.error = error.what();
            std::cerr << component.error << '\n';
        }
        registry.emplace<MaterialComponent>(entity, std::move(component));
    }
    if (description.camera) registry.emplace<CameraComponent>(entity, *description.camera);
    if (description.directionalLight) registry.emplace<DirectionalLightComponent>(entity, *description.directionalLight);
    if (description.pointLight) registry.emplace<PointLightComponent>(entity, *description.pointLight);
    if (description.spotLight) registry.emplace<SpotLightComponent>(entity, *description.spotLight);
    if (description.rigidbody) registry.emplace<RigidbodyComponent>(entity, *description.rigidbody);
    if (description.collider) registry.emplace<ColliderComponent>(entity, *description.collider);
    if (description.script) registry.emplace<ScriptComponent>(entity, *description.script);
    if (description.terrain) registry.emplace<TerrainComponent>(entity, *description.terrain);
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

MaterialDesc SceneSerializer::ReadMaterialAsset(const std::filesystem::path& file)
{
    try
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Cannot open material");
        }
        const auto document = json::parse(stream);
        if (document.at("version") != 1)
        {
            throw std::runtime_error("Unsupported material file version");
        }
        return ReadMaterial(document.at("material"), AssetPaths::ToUtf8(file.stem()));
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error(AssetPaths::ToUtf8(file) + ": " + error.what());
    }
}

namespace
{
    // Keeps a material file on the inheritance chain while its parents resolve.
    class MaterialLoadChainEntry
    {
    public:
        MaterialLoadChainEntry(std::vector<std::filesystem::path>& chain, const std::filesystem::path& file)
            : mChain(chain)
        {
            const auto equivalent = [&](const std::filesystem::path& entry)
            {
                return _wcsicmp(entry.lexically_normal().c_str(), file.lexically_normal().c_str()) == 0;
            };
            const auto repeated = std::find_if(mChain.begin(), mChain.end(), equivalent);
            if (repeated != mChain.end())
            {
                std::string cycle;
                for (auto entry = repeated; entry != mChain.end(); ++entry)
                {
                    cycle += AssetPaths::ToUtf8(entry->filename()) + " -> ";
                }
                cycle += AssetPaths::ToUtf8(file.filename());
                throw std::runtime_error("Material inheritance cycle: " + cycle);
            }
            mChain.push_back(file);
        }

        ~MaterialLoadChainEntry()
        {
            mChain.pop_back();
        }

        MaterialLoadChainEntry(const MaterialLoadChainEntry&) = delete;
        MaterialLoadChainEntry& operator=(const MaterialLoadChainEntry&) = delete;

    private:
        std::vector<std::filesystem::path>& mChain;
    };
}

MaterialID ResourceManager::LoadMaterialAsset(const std::string& path)
{
    const auto file = AssetPaths::Resolve(mSearch->root, path);
    // The chain is checked before the cache: a file that is being saved is never reused stale.
    MaterialLoadChainEntry chainEntry(mMaterialLoadChain, file);
    const auto key = file.native();
    const auto cached = mMaterialAssets.find(key);
    if (cached != mMaterialAssets.end())
    {
        return cached->second;
    }
    // Stamped before reading: an edit that lands during the read is seen by the next reload pass.
    RecordMaterialStamp(file);
    const MaterialDesc description = SceneSerializer::ReadMaterialAsset(file);
    MaterialID material = 0;
    try
    {
        material = CreateTexturedMaterial(description);
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error(AssetPaths::ToUtf8(file) + ": " + error.what());
    }
    mMaterialAssets.emplace(key, material);
    return material;
}

void ResourceManager::RecordMaterialStamp(const std::filesystem::path& file)
{
    MaterialFileStamp stamp;
    std::error_code error;
    stamp.time = std::filesystem::last_write_time(file, error);
    if (!error)
    {
        stamp.size = std::filesystem::file_size(file, error);
    }
    mMaterialStamps[file.native()] = stamp;
}

std::string ResourceManager::GetMaterialAssetError(const std::string& path) const
{
    const auto stamp = mMaterialStamps.find(AssetPaths::Resolve(mSearch->root, path).native());
    return stamp == mMaterialStamps.end() ? std::string() : stamp->second.error;
}

ResourceManager::MaterialReloadResult ResourceManager::ReloadChangedMaterialAssets()
{
    ZoneScopedN("Material hot reload scan");
    MaterialReloadResult result;
    // Rebuilding may load new parent files into mMaterialAssets: iterate over a snapshot.
    const std::vector<std::pair<std::wstring, MaterialID>> assets(mMaterialAssets.begin(), mMaterialAssets.end());
    for (const auto& [key, material] : assets)
    {
        const std::filesystem::path file(key);
        std::error_code error;
        const auto time = std::filesystem::last_write_time(file, error);
        if (error)
        {
            continue; // Missing or locked by an editor mid-save: keep the running version, try again later.
        }
        const auto size = std::filesystem::file_size(file, error);
        if (error)
        {
            continue;
        }
        MaterialFileStamp& stamp = mMaterialStamps[key];
        if (stamp.time == time && stamp.size == size)
        {
            continue;
        }
        stamp.time = time;
        stamp.size = size;
        const std::string name = AssetPaths::ToUtf8(file);
        try
        {
            const MaterialDesc description = SceneSerializer::ReadMaterialAsset(file);
            std::optional<BuiltMaterial> replacement;
            {
                MaterialLoadChainEntry chainEntry(mMaterialLoadChain, file);
                replacement = BuildTexturedMaterial(description);
            }
            // Rejects a new parent that would close a cycle before anything is replaced.
            StoreMaterial(material, std::move(*replacement), description);
            stamp.error.clear();
            result.reloaded.push_back(name);
        }
        catch (const std::exception& reloadError)
        {
            const std::string message = reloadError.what();
            stamp.error = message.rfind(name, 0) == 0 ? message : name + ": " + message;
            result.errors.push_back(stamp.error);
        }
    }
    return result;
}

void ResourceManager::SaveMaterialAsset(const std::string& path, const MaterialDesc& description)
try
{
    const auto file = AssetPaths::Resolve(mSearch->root, path);
    const auto document = json{{"version", 1}, {"material", MaterialToJson(description, mSearch->root)}};
    const auto text = document.dump(2);
    // Resolving the parents before writing rejects missing parents and cycles through this file.
    std::optional<BuiltMaterial> replacement;
    {
        MaterialLoadChainEntry chainEntry(mMaterialLoadChain, file);
        replacement = BuildTexturedMaterial(description);
    }
    const auto cached = mMaterialAssets.find(file.native());
    if (cached != mMaterialAssets.end() && replacement->parent != 0 &&
        (replacement->parent == cached->second || IsMaterialAncestor(cached->second, replacement->parent)))
    {
        throw std::runtime_error("Material inheritance cycle: '" + description.parent + "' already inherits from this file");
    }

    auto temporary = file;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
    try
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << text << '\n';
        stream.close();
        if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            throw std::system_error(GetLastError(), std::system_category(), "Cannot replace material file");
        }
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
    if (cached != mMaterialAssets.end())
    {
        // Every entity using this file and every instance inheriting from it see the change.
        StoreMaterial(cached->second, std::move(*replacement), description);
        // The engine's own write is not an external edit: hot reload must not apply it again.
        RecordMaterialStamp(file);
    }
}
catch (const std::exception& error)
{
    throw std::runtime_error(path + ": " + error.what());
}
