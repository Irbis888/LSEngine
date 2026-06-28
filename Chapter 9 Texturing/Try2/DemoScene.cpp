#include "DemoScene.h"

#include "SceneFactory.h"

void DemoScene::Build(World& world, ResourceManager& resources)
{
    SceneFactory factory(world.registry, resources);

    MeshID sponzaMesh = resources.LoadMesh("../../Common/sponza.obj");

    MaterialID platformMaterial = resources.CreateTexturedMaterial(
        "DemoPlatform",
        L"tile.dds",
        L"tile_nmap.dds",
        glm::vec3(0.55f, 0.55f, 0.58f),
        0.7f);

    MaterialID cubeMaterial = resources.CreateTexturedMaterial(
        "DemoCube",
        L"bricks2.dds",
        L"bricks2_nmap.dds",
        glm::vec3(1.0f),
        0.45f);

    MaterialID sphereMaterial = resources.CreateSolidMaterial(
        "DemoSphere",
        glm::vec3(0.15f, 0.55f, 0.85f),
        0.35f);

    factory.CreateMeshEntity(
        "Sponza",
        sponzaMesh,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(0.3f));

    entt::entity floor = factory.CreatePrimitive(
        "PhysicsFloor",
        PrimitiveType::Plane,
        platformMaterial,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(80.0f, 1.0f, 80.0f));
    world.registry.emplace<RigidbodyComponent>(
        floor,
        RigidbodyComponent{ RigidbodyType::Static });
    world.registry.emplace<ColliderComponent>(
        floor,
        ColliderComponent{
            ColliderType::AABB,
            glm::vec3(0.0f),
            glm::vec3(0.5f, 0.1f, 0.5f),
            0.5f,
            false,
            0.0f,
            0.8f });

    entt::entity cubeA = factory.CreatePrimitive(
        "FallingCubeA",
        PrimitiveType::Cube,
        cubeMaterial,
        glm::vec3(-5.0f, 12.0f, -6.0f),
        glm::vec3(0.0f),
        glm::vec3(2.0f));
    world.registry.emplace<RigidbodyComponent>(
        cubeA,
        RigidbodyComponent{ RigidbodyType::Dynamic, glm::vec3(0.0f), glm::vec3(0.0f), 2.0f, true });
    world.registry.emplace<ColliderComponent>(
        cubeA,
        ColliderComponent{ ColliderType::AABB, glm::vec3(0.0f), glm::vec3(0.5f), 0.5f, false, 0.05f, 0.6f });

    entt::entity cubeB = factory.CreatePrimitive(
        "FallingCubeB",
        PrimitiveType::Cube,
        cubeMaterial,
        glm::vec3(1.0f, 18.0f, -5.0f),
        glm::vec3(0.0f),
        glm::vec3(2.5f));
    world.registry.emplace<RigidbodyComponent>(
        cubeB,
        RigidbodyComponent{ RigidbodyType::Dynamic, glm::vec3(0.8f, 0.0f, 0.0f), glm::vec3(0.0f), 3.0f, true });
    world.registry.emplace<ColliderComponent>(
        cubeB,
        ColliderComponent{ ColliderType::AABB, glm::vec3(0.0f), glm::vec3(0.5f), 0.5f, false, 0.05f, 0.6f });

    entt::entity sphereA = factory.CreatePrimitive(
        "FallingSphereA",
        PrimitiveType::Sphere,
        sphereMaterial,
        glm::vec3(5.0f, 15.0f, -4.0f),
        glm::vec3(0.0f),
        glm::vec3(2.0f));
    world.registry.emplace<RigidbodyComponent>(
        sphereA,
        RigidbodyComponent{ RigidbodyType::Dynamic, glm::vec3(-0.5f, 0.0f, 0.0f), glm::vec3(0.0f), 1.0f, true });
    world.registry.emplace<ColliderComponent>(
        sphereA,
        ColliderComponent{ ColliderType::Sphere, glm::vec3(0.0f), glm::vec3(0.5f), 0.5f, false, 0.2f, 0.4f });

    entt::entity sphereB = factory.CreatePrimitive(
        "FallingSphereB",
        PrimitiveType::Sphere,
        sphereMaterial,
        glm::vec3(-1.0f, 23.0f, 1.0f),
        glm::vec3(0.0f),
        glm::vec3(1.5f));
    world.registry.emplace<RigidbodyComponent>(
        sphereB,
        RigidbodyComponent{ RigidbodyType::Dynamic, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f, true });
    world.registry.emplace<ColliderComponent>(
        sphereB,
        ColliderComponent{ ColliderType::Sphere, glm::vec3(0.0f), glm::vec3(0.5f), 0.5f, false, 0.2f, 0.4f });

    entt::entity sun = factory.CreateEmpty("SunLight");
    world.registry.emplace<DirectionalLightComponent>(
        sun,
        DirectionalLightComponent{
            glm::vec3(1.0f, 0.96f, 0.88f),
            1.15f,
            glm::normalize(glm::vec3(-0.55f, -0.8f, 0.25f)),
            true });

    entt::entity warmPoint = factory.CreateEmpty(
        "WarmPointLight",
        glm::vec3(-8.0f, 7.0f, -8.0f));
    world.registry.emplace<PointLightComponent>(
        warmPoint,
        PointLightComponent{
            glm::vec3(1.0f, 0.45f, 0.25f),
            3.0f,
            2.0f,
            35.0f,
            true });

    entt::entity coolPoint = factory.CreateEmpty(
        "CoolPointLight",
        glm::vec3(7.0f, 6.0f, 3.0f));
    world.registry.emplace<PointLightComponent>(
        coolPoint,
        PointLightComponent{
            glm::vec3(0.2f, 0.55f, 1.0f),
            2.5f,
            2.0f,
            30.0f,
            true });

    entt::entity spot = factory.CreateEmpty(
        "SpotLight",
        glm::vec3(0.0f, 14.0f, -10.0f));
    world.registry.emplace<SpotLightComponent>(
        spot,
        SpotLightComponent{
            glm::vec3(1.0f, 0.9f, 0.7f),
            2.0f,
            glm::normalize(glm::vec3(0.0f, -0.9f, 0.35f)),
            2.0f,
            45.0f,
            24.0f,
            true });

    factory.CreateCamera(
        "MainCamera",
        glm::vec3(-1.0f, 10.0f, -25.0f),
        glm::vec3(0.0f),
        1.8f,
        0.1f,
        1500.0f,
        4.0f / 3.0f);
}
