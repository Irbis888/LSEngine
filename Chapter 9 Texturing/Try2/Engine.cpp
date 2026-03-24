#include "Engine.h"

void Engine::Init() {
	renderSystems.push_back(std::make_unique<RenderSystem>(&mRenderAdapter));

	// Create first mesh entity
	auto e1 = world.registry.create();
	world.registry.emplace<TagComponent>(e1, TagComponent{ "MeshA" });
	world.registry.emplace<MeshComponent>(e1, MeshComponent{ 1 });
	world.registry.emplace<TransformComponent>(e1, TransformComponent{ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f) });

	// Create second mesh entity with different name and position
	auto e2 = world.registry.create();
	world.registry.emplace<TagComponent>(e2, TagComponent{ "MeshB" });
	world.registry.emplace<MeshComponent>(e2, MeshComponent{ 2 });
	world.registry.emplace<TransformComponent>(e2, TransformComponent{ glm::vec3(5.0f, 1.0f, -3.0f), glm::vec3(0.0f), glm::vec3(1.0f) });
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

