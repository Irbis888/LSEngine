#include "Engine.h"
#include "TextureBenchmark.h"
#include "RenderSystem.h"
#include "CameraControllerSystem.h"
#include "DemoScene.h"
#include "PhysicsSystem.h"
#include "Editor/EditorContext.h"
#include "SceneSerializer.h"

#include <filesystem>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <cmath>

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

struct Engine::PendingScene
{
    struct Result
    {
        SceneSerializer::SceneData data;
        std::string error;
    };
    std::shared_ptr<Result> result;
    JobSystem::TaskHandle task;
    bool publishing = false;
};

Engine::Engine(IRenderAdapter* renderer) : mRenderAdapter(renderer) {}

void Engine::Init(const GameTimer& gt) {
	const uint32_t hardwareThreads = std::thread::hardware_concurrency();
	mJobs.Init(std::min<uint32_t>(4, hardwareThreads > 1 ? hardwareThreads - 1 : 1));
	mResourceManager.InitLoading(mJobs);
	mRenderAdapter->SetResourceManager(&mResourceManager);
	updateSystems.push_back(std::make_unique<CameraControllerSystem>());
	physicsSystems.push_back(std::make_unique<PhysicsSystem>());
	renderSystems.push_back(std::make_unique<RenderSystem>(mRenderAdapter));

	if (const std::filesystem::path* scenePath = FindScenePath())
	{
        std::string error;
        mStartupScene = true;
        LoadScene(scenePath->string(), error);
	}
	else DemoScene::Build(world, mResourceManager);
}

Engine::~Engine()
{
	Shutdown();
}
void Engine::Shutdown()
{
	mResourceManager.ShutdownLoading();
	mJobs.Shutdown();
    mPendingScene.reset();
    mSceneProgress.active = false;
    mRetiredWorld.reset();
	mRenderAdapter->SetResourceManager(nullptr);
}
void Engine::Update(const FrameContext& context)
{
    PumpSceneLoading();
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
	if (IsSceneLoading() || !Editor_IsPhysicsEnabled())
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

bool Engine::SaveScene(const std::string& path, std::string& outError)
{
    if (IsSceneLoading()) { outError = "Scene is still loading"; return false; }
	try
	{
		SceneSerializer::Save(world, path);
		outError.clear();
		return true;
	}
	catch (const std::exception& e)
	{
		outError = e.what();
		return false;
	}
}

bool Engine::LoadScene(const std::string& path, std::string& outError)
{
    if (!mJobs.IsInitialized()) { outError = "Engine is not initialized"; return false; }
    if (IsSceneLoading()) { outError = "A scene load is already in progress"; return false; }
    try
    {
        auto pending = std::make_unique<PendingScene>();
        pending->result = std::make_shared<PendingScene::Result>();
        const auto absolute = std::filesystem::absolute(std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(path.data()), path.size()))).u8string();
        const std::string filename(reinterpret_cast<const char*>(absolute.data()), absolute.size());
        pending->task = mJobs.Submit([result = pending->result, filename]
        {
            try { result->data = SceneSerializer::Read(filename); }
            catch (const std::exception& error) { result->error = error.what(); }
            catch (...) { result->error = "Unknown scene loading error"; }
        });
        mPendingScene = std::move(pending);
        mSceneProgress = { true, 0, 0, path, {} };
        TextureBenchmark::Begin(path);
        outError.clear();
        return true; // Accepted: completion/errors arrive through SceneLoadProgress.
    }
    catch (const std::exception& error) { outError = error.what(); return false; }
}

void Engine::SetSceneLoadingBudget(const SceneLoadingBudget& budget)
{
    if (!std::isfinite(budget.initialMilliseconds) || budget.initialMilliseconds <= 0 ||
        !std::isfinite(budget.streamingMilliseconds) || budget.streamingMilliseconds <= 0)
        throw std::invalid_argument("Scene loading budgets must be positive");
    mSceneLoadingBudget = budget;
}

void Engine::PumpSceneLoading()
{
    ZoneScopedN("Bounded scene entity publication");
    const auto start = std::chrono::steady_clock::now();
    const double budgetMs = mStartupScene ? mSceneLoadingBudget.initialMilliseconds : mSceneLoadingBudget.streamingMilliseconds;
    const auto overBudget = [&]
    { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= budgetMs; };
    // Retire the previous registry gradually instead of destroying every
    // component inside the scene-switch button callback.
    while (mRetiredWorld && !overBudget())
    {
        const auto entities = mRetiredWorld->registry.storage<entt::entity>().each();
        if (entities.begin() == entities.end()) { mRetiredWorld.reset(); break; }
        const auto [entity] = *entities.begin();
        mRetiredWorld->registry.destroy(entity);
    }
    if (!mPendingScene) return;
    auto& pending = *mPendingScene;
    if (!pending.publishing)
    {
        if (!pending.task.IsComplete()) return; // Keep drawing the current scene.
        mJobs.Wait(pending.task); // Completed only; never execute parser in a frame.
        if (!pending.result->error.empty())
        {
            mSceneProgress.error = pending.result->error;
            mSceneProgress.active = false;
            std::cerr << mSceneProgress.error << '\n';
            mPendingScene.reset();
            if (mStartupScene) DemoScene::Build(world, mResourceManager);
            mStartupScene = false;
            return;
        }
        // Finish retiring an older switch before exchanging registries again.
        if (mRetiredWorld) return;
        mRetiredWorld = std::make_unique<World>();
        world.registry.swap(mRetiredWorld->registry);
        mSceneProgress.total = pending.result->data.entities.size();
        pending.publishing = true;
    }
    try
    {
        size_t batch = 0;
        while (mSceneProgress.created < mSceneProgress.total &&
            (batch == 0 || !overBudget()))
        {
            SceneSerializer::CreateEntity(world, mResourceManager,
                pending.result->data.entities[mSceneProgress.created]);
            ++mSceneProgress.created;
            ++batch;
        }
        TracyPlot("Scene entities created", int64_t(mSceneProgress.created));
        if (mSceneProgress.created == mSceneProgress.total)
        {
            mSceneProgress.active = false;
            mStartupScene = false;
            std::cout << "Loaded scene from " << mSceneProgress.path << '\n';
            mPendingScene.reset();
        }
    }
    catch (const std::exception& error)
    {
        mSceneProgress.error = error.what();
        mSceneProgress.active = false;
        mStartupScene = false;
        std::cerr << mSceneProgress.error << '\n';
        mPendingScene.reset();
    }
}
