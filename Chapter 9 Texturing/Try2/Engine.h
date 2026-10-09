#pragma once

#include "Commons.h"
#include "World.h"
#include "ResourceManager.h"
#include "JobSystem.h"

class Engine
{
private:
    struct PendingScene;
    std::unique_ptr<PendingScene> mPendingScene;
    std::unique_ptr<World> mRetiredWorld;
    void PumpSceneLoading();
    bool mStartupScene = false;
	World world;

	std::vector<std::unique_ptr<ISystem>> updateSystems;
	std::vector<std::unique_ptr<ISystem>> physicsSystems;
	std::vector<std::unique_ptr<ISystem>> renderSystems;

    IRenderAdapter* mRenderAdapter;
	JobSystem mJobs; // Must outlive ResourceManager's submitted work.
	ResourceManager mResourceManager;
public:
    Engine(IRenderAdapter* renderer);
    ~Engine();
    void Shutdown();
    struct SceneLoadProgress
    {
        bool active = false;
        size_t created = 0;
        size_t total = 0;
        std::string path;
        std::string error;
    };
    const SceneLoadProgress& GetSceneLoadProgress() const { return mSceneProgress; }
    bool IsSceneLoading() const { return mSceneProgress.active; }
    struct SceneLoadingBudget
    {
        double initialMilliseconds = 8.0;
        double streamingMilliseconds = 4.0;
    };
    void SetSceneLoadingBudget(const SceneLoadingBudget& budget);
    const SceneLoadingBudget& GetSceneLoadingBudget() const { return mSceneLoadingBudget; }

	void Init(const GameTimer& gt);
	void Update(const FrameContext& context);
	void PhysicsUpdate(const FrameContext& context);
	void Draw(const FrameContext& context);

	World& GetWorld() { return world; }
	entt::registry& GetRegistry() { return world.registry; }
	ResourceManager& GetResources() { return mResourceManager; }

	bool SaveScene(const std::string& path, std::string& outError);
	bool LoadScene(const std::string& path, std::string& outError);
private:
    SceneLoadProgress mSceneProgress;
    SceneLoadingBudget mSceneLoadingBudget;
};
