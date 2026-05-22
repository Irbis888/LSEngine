#include "Engine.h"
#include "RenderSystem.h"
#include "CameraControllerSystem.h"
#include "DemoScene.h"
#include "PhysicsSystem.h"

void Engine::Init(const GameTimer& gt) {
	mRenderAdapter->SetResourceManager(&mResourceManager);
	updateSystems.push_back(std::make_unique<CameraControllerSystem>());
	physicsSystems.push_back(std::make_unique<PhysicsSystem>());
	renderSystems.push_back(std::make_unique<RenderSystem>(mRenderAdapter));

	DemoScene::Build(world, mResourceManager);

	/*mResourceManager.PrintAllMeshes();
	mResourceManager.PrintAllTextures();
	mResourceManager.PrintAllMaterials();*/


}
void Engine::Update(const FrameContext& context)
{
	if (context.input.keysPressed[VK_F5])
	{
		mRenderAdapter->ReloadShaders();
	}

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

