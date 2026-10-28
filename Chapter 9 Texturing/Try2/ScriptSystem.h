#pragma once
#include "ScriptRuntime.h"
#include <map>
#include <unordered_set>

class ScriptSystem final : public ISystem
{
public:
    explicit ScriptSystem(std::filesystem::path root);
    ~ScriptSystem();
    void Bind(entt::registry& registry);
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
private:
    void Sync();
    void DeliverEvents();
    void FlushDestructions();
    void RemoveInstance(entt::registry& registry, entt::entity entity);
    void DestroyAll();
    void Disconnect();
    ScriptRuntime mRuntime;
    entt::registry* mRegistry = nullptr;
    bool mPlaying = false, mReady = true, mStopping = false;
    std::map<entt::entity, ScriptRuntime::Instance> mInstances;
    std::unordered_set<entt::entity> mDestroying;
    std::map<std::string, ScriptRuntime::Description> mDescriptions;
    std::map<entt::entity, std::string> mLastErrors;
    entt::scoped_connection mRemoval;
};
