#include "SceneSerializer.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void Verify(World& world, ResourceManager& resources)
{
    size_t cubes = 0, spheres = 0, floors = 0, meshes = 0, cameras = 0, suns = 0;
    for (auto entity : world.registry.view<MeshComponent>())
    {
        ++meshes;
        auto& mesh = resources.GetMesh(world.registry.get<MeshComponent>(entity).meshID);
        if (mesh.vertices.empty() || mesh.indices.empty())
            throw std::runtime_error("Empty scene mesh");
        const auto& collider = world.registry.get<ColliderComponent>(entity);
        const auto& body = world.registry.get<RigidbodyComponent>(entity);
        if (body.type == RigidbodyType::Static) { ++floors; continue; }
        if (collider.type == ColliderType::Sphere)
        {
            ++spheres;
            for (const auto& vertex : mesh.vertices)
                if (std::abs(glm::length(vertex.Position) - collider.radius) > 1e-4f)
                    throw std::runtime_error("Sphere mesh and collider sizes differ");
        }
        else
        {
            ++cubes;
            for (const auto& vertex : mesh.vertices)
                if (glm::any(glm::greaterThan(glm::abs(vertex.Position), collider.halfExtents + glm::vec3(1e-4f))))
                    throw std::runtime_error("Cube mesh exceeds collider");
        }
    }
    for (auto entity : world.registry.view<CameraComponent>()) { (void)entity; ++cameras; }
    for (auto entity : world.registry.view<DirectionalLightComponent>()) { (void)entity; ++suns; }
    if (cubes != 256 || spheres != 256 || floors != 1 || meshes != 513 || cameras != 1 || suns != 1)
        throw std::runtime_error("Mixed scene components/counts are wrong");
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("Expected scene and temporary snapshot paths");
        World world;
        ResourceManager resources;
        SceneSerializer::Load(world, resources, argv[1]);
        Verify(world, resources);
        SceneSerializer::Save(world, argv[2]);
        // Play snapshots reference the existing ResourceManager's mesh IDs.
        World restored;
        SceneSerializer::Load(restored, resources, argv[2]);
        Verify(restored, resources);
        std::cout << "PASS: native scene loader and Play snapshot restore; 256 cubes + 256 spheres + floor, camera, sun\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

