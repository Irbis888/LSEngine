#include "Engine.h"
#include "RenderSystem.h"
#include "CameraControllerSystem.h"

void Engine::Init(const GameTimer& gt) {
	mRenderAdapter->SetResourceManager(&mResourceManager);
	updateSystems.push_back(std::make_unique<CameraControllerSystem>());
	renderSystems.push_back(std::make_unique<RenderSystem>(mRenderAdapter));
	MeshID sponzaMesh = mResourceManager.LoadMesh("../../Common/sponza.obj");

	Material primitiveMaterial;
	primitiveMaterial.name = "PrimitiveDefault";
	primitiveMaterial.albedo = mResourceManager.LoadTexture(L"white1x1.dds");
	primitiveMaterial.normal = mResourceManager.LoadTexture(L"default_nmap.dds");
	primitiveMaterial.color = glm::vec3(1.0f);
	primitiveMaterial.roughness = 0.6f;

	MaterialID primitiveMaterialId = mResourceManager.CreateMaterial(primitiveMaterial);
	MeshID planeMesh = mResourceManager.CreatePlane(primitiveMaterialId);
	MeshID cubeMesh = mResourceManager.CreateCube(primitiveMaterialId);
	MeshID sphereMesh = mResourceManager.CreateSphere(primitiveMaterialId);

	auto createMeshEntity = [this](const std::string& tag, MeshID mesh, const glm::vec3& position, const glm::vec3& scale)
		{
			auto entity = world.registry.create();
			world.registry.emplace<TagComponent>(entity, TagComponent{ tag });
			world.registry.emplace<MeshComponent>(entity, MeshComponent{ mesh });
			world.registry.emplace<TransformComponent>(entity, TransformComponent{ position, glm::vec3(0.0f), scale });
			return entity;
		};

	// Create first mesh entity
	createMeshEntity("Sponza", sponzaMesh, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.3f));
	createMeshEntity("PrimitivePlane", planeMesh, glm::vec3(-8.0f, 1.1f, -8.0f), glm::vec3(10.0f, 1.0f, 10.0f));
	createMeshEntity("PrimitiveCube", cubeMesh, glm::vec3(-1.0f, 9.0f, -20.0f), glm::vec3(10.0f));
	createMeshEntity("PrimitiveSphere", sphereMesh, glm::vec3(-2.0f, 1.0f, -2.0f), glm::vec3(10.0f));

	auto cam = world.registry.create();
	world.registry.emplace<TagComponent>(cam, TagComponent{ "MainCamera" });
	world.registry.emplace<CameraComponent>(cam, CameraComponent{ 1.8f, 0.1f, 1500.0f, 4.0f / 3.0f });
	world.registry.emplace<TransformComponent>(cam, TransformComponent{ glm::vec3(-1.0f, 11.0f, -20.0f), glm::vec3(0.0f), glm::vec3(1.0f) });


	mResourceManager.PrintAllMeshes();
	mResourceManager.PrintAllTextures();
	mResourceManager.PrintAllMaterials();


}
void Engine::Update(const FrameContext& context)
{
	for (auto& system : updateSystems)
	{
		system->Update(world.registry, context);
	}
}
void Engine::PhysicsUpdate(const FrameContext& context)
{
	for (auto& system : physicsSystems)
	{
		system->Update(world.registry, context);
	}
}

void Engine::Draw(const FrameContext& context)
{
	for (auto& system : renderSystems)
	{
		system->Update(world.registry, context);
	}
}

