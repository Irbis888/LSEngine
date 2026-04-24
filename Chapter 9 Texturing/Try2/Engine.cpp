#include "Engine.h"
#include "RenderSystem.h"

void Engine::Init(const GameTimer& gt) {
	mRenderAdapter->SetResourceManager(&mResourceManager);
	renderSystems.push_back(std::make_unique<RenderSystem>(mRenderAdapter));
	mResourceManager.LoadMesh("../../Common/sponza.obj");

	// Create first mesh entity
	auto sponza = world.registry.create();
	world.registry.emplace<TagComponent>(sponza, TagComponent{ "Sponza" });
	world.registry.emplace<MeshComponent>(sponza, MeshComponent{ 1 });
	world.registry.emplace<TransformComponent>(sponza, TransformComponent{ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f) });

	auto cam = world.registry.create();
	world.registry.emplace<TagComponent>(cam, TagComponent{ "MainCamera" });
	world.registry.emplace<CameraComponent>(cam, CameraComponent{ 90.0f, 0.1f, 1500.0f, 4.0f / 3.0f });
	world.registry.emplace<TransformComponent>(cam, TransformComponent{ glm::vec3(-1.0f, 11.0f, -20.0f), glm::vec3(0.0f), glm::vec3(1.0f) });


	mResourceManager.PrintAllMeshes();
	mResourceManager.PrintAllTextures();
	mResourceManager.PrintAllMaterials();


}
void Engine::Update(const GameTimer& gt)
{
	for (auto& system : updateSystems)
	{
		system->Update(world.registry, gt);
	}
}
void Engine::PhysicsUpdate(const GameTimer& gt, float dt)
{
	for (auto& system : physicsSystems)
	{
		system->Update(world.registry, gt);
	}
}

void Engine::Draw(const GameTimer& gt)
{
	for (auto& system : renderSystems)
	{
		system->Update(world.registry, gt);
	}
}

