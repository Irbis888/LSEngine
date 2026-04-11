#include "Engine.h"
#include "RenderSystem.h"

void Engine::Init() {
	mRenderAdapter->SetResourceManager(&mResourceManager);
	renderSystems.push_back(std::make_unique<RenderSystem>(mRenderAdapter));
	mResourceManager.LoadMesh("../../Common/sponza.obj");

	// Create first mesh entity
	auto sponza = world.registry.create();
	world.registry.emplace<TagComponent>(sponza, TagComponent{ "Sponza" });
	world.registry.emplace<MeshComponent>(sponza, MeshComponent{ 1 });
	world.registry.emplace<TransformComponent>(sponza, TransformComponent{ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f) });

	mResourceManager.PrintAllMeshes();
	mResourceManager.PrintAllTextures();
	mResourceManager.PrintAllMaterials();


}
void Engine::Update(const GameTimer& gt)
{
	for (auto& system : updateSystems)
	{
		system->Update(world.registry, gt.DeltaTime());
	}
}
void Engine::PhysicsUpdate(float dt)
{
	for (auto& system : physicsSystems)
	{
		system->Update(world.registry, dt);
	}
}

void Engine::Draw(const GameTimer& gt)
{
	for (auto& system : renderSystems)
	{
		system->Update(world.registry, gt.DeltaTime());
	}
}

