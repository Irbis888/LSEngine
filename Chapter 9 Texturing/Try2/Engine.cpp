#include "Engine.h"
#include "TextureBenchmark.h"
#include "RenderSystem.h"
#include "CameraControllerSystem.h"
#include "DemoScene.h"
#include "PhysicsSystem.h"
#include "Editor/EditorContext.h"
#include "SceneSerializer.h"
#include "ScriptSystem.h"
#include "AssetPaths.h"

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

Engine::Engine(IRenderAdapter* renderer, std::filesystem::path assetRoot)
    : mRenderAdapter(renderer), mAssetRoot(assetRoot.empty() ? AssetPaths::FindRoot() : std::filesystem::weakly_canonical(assetRoot)) {}

void Engine::Init(const GameTimer& gt) {
	const uint32_t hardwareThreads = std::thread::hardware_concurrency();
	mJobs.Init(std::min<uint32_t>(15, hardwareThreads > 1 ? hardwareThreads - 1 : 1));
	mResourceManager.InitLoading(mJobs);
	mRenderAdapter->SetResourceManager(&mResourceManager);
	updateSystems.push_back(std::make_unique<CameraControllerSystem>());
    auto scripts = std::make_unique<ScriptSystem>(mAssetRoot);
    auto* scriptSystem = scripts.get();
    updateSystems.push_back(std::move(scripts));
    mScripts = scriptSystem;
    mScripts->Bind(world.registry);
	physicsSystems.push_back(std::make_unique<PhysicsSystem>(mJobs));
	renderSystems.push_back(std::make_unique<RenderSystem>(mRenderAdapter));

	if (const std::filesystem::path* scenePath = FindScenePath())
	{
        std::string error;
        mStartupScene = true;
        LoadScene(AssetPaths::ToUtf8(std::filesystem::absolute(*scenePath)), error);
	}
	else { DemoScene::Build(world, mResourceManager); mScripts->SceneReady(); }
}

Engine::~Engine()
{
	Shutdown();
}
void Engine::Shutdown()
{
    EndPlay(); // OnDestroy runs while the world and resources still exist.
	mResourceManager.ShutdownLoading();
	mJobs.Shutdown();
    mPendingScene.reset();
    mSceneProgress.active = false;
    mQueuedRestore.clear();
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
        if (system.get() == mScripts && IsSceneLoading()) continue;
		system->Update(world.registry, context);
	}
}
void Engine::PhysicsUpdate(const FrameContext& context)
{
	if (IsSceneLoading() || (!IsPlaying() && !Editor_IsPhysicsEnabled()))
		return;

    if (mScripts) mScripts->FixedUpdate(context);
	for (auto& system : physicsSystems)
	{
		system->Update(world.registry, context);
	}
}

PhysicsSystem& Engine::GetPhysics()
{
    if (physicsSystems.empty()) throw std::logic_error("Physics is not initialized");
    return static_cast<PhysicsSystem&>(*physicsSystems.front());
}

ScriptSystem& Engine::GetScripts()
{
    if (!mScripts) throw std::logic_error("Scripting is not initialized");
    return *mScripts;
}
bool Engine::BeginPlay(std::string& outError)
{
    if (!mScripts || IsSceneLoading()) { outError = "Wait for the complete scene before Play"; return false; }
    try { mScripts->BeginPlay(); outError.clear(); return true; }
    catch (const std::exception& error) { outError = error.what(); return false; }
}
void Engine::EndPlay() { if (mScripts) mScripts->EndPlay(); }
bool Engine::IsPlaying() const { return mScripts && mScripts->IsPlaying(); }
bool Engine::RestorePlaySnapshot(const std::string& path, std::string& outError)
{
    EndPlay();
    try
    {
        const auto absolute = AssetPaths::Resolve(mAssetRoot, path);
        // Validate now. A parse failure keeps the current stopped scene intact.
        if (!std::filesystem::is_regular_file(absolute)) throw std::runtime_error("Play snapshot is missing: " + path);
        const auto filename = AssetPaths::ToUtf8(absolute);
        if (mPendingScene) { mQueuedRestore = filename; outError.clear(); return true; }
        return LoadScene(filename, outError);
    }
    catch (const std::exception& error) { outError = error.what(); return false; }
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
		SceneSerializer::Save(world, AssetPaths::ToUtf8(AssetPaths::Resolve(mAssetRoot, path)), mAssetRoot);
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
        const std::string filename = AssetPaths::ToUtf8(AssetPaths::Resolve(mAssetRoot, path));
        pending->task = mJobs.Submit([result = pending->result, filename]
        {
            try { result->data = SceneSerializer::Read(filename); }
            catch (const std::exception& error) { result->error = error.what(); }
            catch (...) { result->error = "Unknown scene loading error"; }
        }, JobSystem::Priority::Low);
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
    if (!mPendingScene && !mQueuedRestore.empty())
    {
        auto path = std::move(mQueuedRestore);
        mQueuedRestore.clear();
        std::string error;
        if (!LoadScene(path, error)) mSceneProgress.error = "Snapshot restore failed: " + error;
    }
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
            if (mStartupScene) { DemoScene::Build(world, mResourceManager); mScripts->SceneReady(); }
            mStartupScene = false;
            return;
        }
        // Finish retiring an older switch before exchanging registries again.
        if (mRetiredWorld) return;
        mRetiredWorld = std::make_unique<World>();
        mScripts->BeforeSceneChange();
        world.registry.swap(mRetiredWorld->registry);
        mScripts->Bind(world.registry);
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
            if (mQueuedRestore.empty()) mScripts->SceneReady();
        }
    }
    catch (const std::exception& error)
    {
        mSceneProgress.error = error.what();
        mSceneProgress.active = false;
        mStartupScene = false;
        std::cerr << mSceneProgress.error << '\n';
        mPendingScene.reset();
        // A failed publication stays stopped rather than initializing a partial scene.
        EndPlay();
    }
}
