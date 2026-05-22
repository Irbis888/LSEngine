#include "Engine.h"
#include "RenderSystem.h"
#include "CameraControllerSystem.h"
#include "DemoScene.h"
#include "PhysicsSystem.h"
#include "Editor/EditorContext.h"
#include "SceneSerializer.h"

#include <filesystem>
#include <iostream>

namespace
{
	const std::filesystem::path* FindScenePath()
	{
		static const std::filesystem::path candidates[] =
		{
			"Scenes/DemoScene.json",
			"../Scenes/DemoScene.json",
			"../../Scenes/DemoScene.json",
			"Chapter 9 Texturing/Try2/Scenes/DemoScene.json"
		};

		for (const auto& candidate : candidates)
		{
			if (std::filesystem::exists(candidate))
			{
				return &candidate;
			}
		}

		return nullptr;
	}
}

void Engine::Init(const GameTimer& gt) {
	mRenderAdapter->SetResourceManager(&mResourceManager);
	updateSystems.push_back(std::make_unique<CameraControllerSystem>());
	physicsSystems.push_back(std::make_unique<PhysicsSystem>());
	renderSystems.push_back(std::make_unique<RenderSystem>(mRenderAdapter));

	if (const std::filesystem::path* scenePath = FindScenePath())
	{
		try
		{
			SceneSerializer::Load(world, mResourceManager, scenePath->string());
			std::cout << "Loaded scene from " << scenePath->string() << std::endl;
		}
		catch (const std::exception& e)
		{
			std::cout << "Failed to load scene file, using DemoScene fallback: " << e.what() << std::endl;
			world.registry.clear();
			DemoScene::Build(world, mResourceManager);
		}
	}
	else
	{
		std::cout << "Scene file not found, using DemoScene fallback." << std::endl;
		DemoScene::Build(world, mResourceManager);
	}

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
	if (!Editor_IsPhysicsEnabled())
		return;

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

