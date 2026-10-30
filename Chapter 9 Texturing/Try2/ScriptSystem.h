#pragma once
#include "ScriptRuntime.h"
#include "JobSystem.h"
#include <chrono>
#include <map>
#include <unordered_set>
#include <utility>

class ScriptSystem final : public ISystem
{
public:
    explicit ScriptSystem(std::filesystem::path root);
    ~ScriptSystem();
    void Bind(entt::registry& registry);
    void SetResources(ResourceManager* resources) { mResources = resources; } // Needed by Entity.Spawn meshes.
    void BeforeSceneChange();
    void SceneReady();
    void BeginPlay();
    void EndPlay();
    bool IsPlaying() const { return mPlaying; }
    void Update(entt::registry& registry, const FrameContext& context) override;
    void FixedUpdate(const FrameContext& context);
    const ScriptRuntime::Description& Describe(const std::string& path, bool reload = false);
    std::string GetStatus(entt::entity entity) const;
    std::string GetError(entt::entity entity) const;
    void DestroyEntity(entt::entity entity);
    ScriptRuntime& GetRuntime() { return mRuntime; }

    // Hot reload: a JobSystem task checks the watched Lua/prefab files, reads changed ones and
    // compiles Lua in a private interpreter; PumpHotReload applies results on the main thread,
    // outside callbacks. In Play, instances of a changed script restart (OnDestroy, fresh
    // state, OnInit); a changed prefab affects later spawns. Invalid text is rejected and the
    // running version stays. Engine calls the pump once per frame while no scene is loading.
    void SetJobs(JobSystem* jobs) { mJobs = jobs; }
    void SetHotReload(bool enabled) { mHotReload = enabled; }
    bool IsHotReloadEnabled() const { return mHotReload; }
    void PumpHotReload();
    void StopHotReload(); // Waits for an in-flight scan; Engine calls it before JobSystem shutdown.
    std::string TakeHotReloadNotice() { return std::exchange(mHotReloadNotice, {}); }
    uint64_t GetHotReloadCount() const { return mHotReloadCount; }
private:
    struct FileStamp
    {
        std::filesystem::file_time_type time{};
        uintmax_t size = 0;
        bool operator==(const FileStamp&) const = default;
    };
    struct HotReloadScan;
    void SubmitHotReloadScan();
    void ApplyHotReload(HotReloadScan& scan);
    void ReloadScript(const std::string& path, std::string source);
    void Notify(const std::string& message, bool error);
    void Sync();
    void DeliverEvents();
    void FlushDestructions();
    void FlushSpawns();
    void DiscardSpawns();
    void RemoveInstance(entt::registry& registry, entt::entity entity);
    void DestroyAll();
    void Disconnect();
    ScriptRuntime mRuntime;
    entt::registry* mRegistry = nullptr;
    ResourceManager* mResources = nullptr;
    bool mPlaying = false, mReady = true, mStopping = false;
    std::map<entt::entity, ScriptRuntime::Instance> mInstances;
    std::unordered_set<entt::entity> mDestroying;
    std::map<std::string, ScriptRuntime::Description> mDescriptions;
    std::map<entt::entity, std::string> mLastErrors;
    std::map<entt::entity, TransformComponent> mInitialTransforms; // Per Play session, for hot reload reset.
    entt::scoped_connection mRemoval;
    JobSystem* mJobs = nullptr;
    bool mHotReload = true;
    JobSystem::TaskHandle mScanTask;
    std::shared_ptr<HotReloadScan> mScan;
    std::map<std::string, FileStamp> mStamps; // "script:<path>" / "prefab:<path>" -> last seen on disk.
    std::chrono::steady_clock::time_point mNextScan{};
    std::string mHotReloadNotice;
    uint64_t mHotReloadCount = 0;
};
