#include "ScriptSystem.h"
#include "ImGuiBridge.h"

ScriptSystem::ScriptSystem(std::filesystem::path root) : mRuntime(std::move(root)) {}
ScriptSystem::~ScriptSystem() { EndPlay(); Disconnect(); mRuntime.Unbind(); }
void ScriptSystem::Disconnect() { mRemoval.release(); }
void ScriptSystem::Bind(entt::registry& registry)
{
    Disconnect();
    mRegistry = &registry;
    mLastErrors.clear();
    mRuntime.Bind(registry);
    // EnTT removes pools in reverse creation order. Create every component
    // exposed to Lua before ScriptComponent, so even registry.destroy/clear
    // runs OnDestroy while Tag/Transform/Rigidbody data are still available.
    registry.storage<TagComponent>();
    registry.storage<TransformComponent>();
    registry.storage<RigidbodyComponent>();
    mRemoval = registry.on_destroy<ScriptComponent>().connect<&ScriptSystem::RemoveInstance>(*this);
    mReady = false;
}
void ScriptSystem::BeforeSceneChange()
{
    DestroyAll(); // Old registry and components are still intact here.
    Disconnect(); // EnTT storage/signals are swapped with the registry contents.
    mRuntime.Unbind();
    mRegistry = nullptr;
    mReady = false;
}
void ScriptSystem::SceneReady()
{
    mReady = true;
    if (mPlaying) { Sync(); FlushDestructions(); }
}
void ScriptSystem::BeginPlay()
{
    if (!mRegistry || !mReady) throw std::logic_error("Cannot start scripts on a partially loaded scene");
    if (mPlaying) return;
    mRuntime.Bind(*mRegistry); // New session generation, even without a scene swap.
    mPlaying = true;
    mLastErrors.clear();
    Sync();
    FlushDestructions();
}
void ScriptSystem::EndPlay()
{
    mPlaying = false;
    DestroyAll();
    if (mRegistry) mRuntime.Bind(*mRegistry); // Invalidate references from the closed session.
}
void ScriptSystem::DestroyAll()
{
    mStopping = true;
    mRuntime.ClearCommands();
    while (!mInstances.empty())
    {
        const auto entity = mInstances.begin()->first;
        if (mRegistry && mRegistry->valid(entity)) RemoveInstance(*mRegistry, entity);
        else { mRuntime.Release(mInstances.begin()->second); mInstances.erase(mInstances.begin()); }
    }
    mRuntime.ClearCommands(); // Discard commands/events emitted by OnDestroy during teardown.
    mStopping = false;
}
void ScriptSystem::RemoveInstance(entt::registry&, entt::entity entity)
{
    auto it = mInstances.find(entity);
    if (it == mInstances.end() || mDestroying.contains(entity)) return;
    mDestroying.insert(entity);
    mRuntime.Call(it->second, "OnDestroy");
    if (!it->second.error.empty()) mLastErrors[entity] = it->second.error;
    mRuntime.Release(it->second);
    mInstances.erase(it);
    mDestroying.erase(entity);
}
void ScriptSystem::Sync()
{
    if (!mRegistry || !mReady || !mPlaying) return;
    for (auto it = mInstances.begin(); it != mInstances.end();)
    {
        const auto entity = it->first;
        const auto* component = mRegistry->valid(entity) ? mRegistry->try_get<ScriptComponent>(entity) : nullptr;
        if (!component || component->path != it->second.path)
        {
            ++it;
            if (mRegistry->valid(entity)) RemoveInstance(*mRegistry, entity);
            else { auto dead = mInstances.find(entity); mRuntime.Release(dead->second); mInstances.erase(dead); }
        }
        else ++it;
    }
    // Snapshot IDs; callbacks cannot invalidate the view (Destroy is deferred).
    const auto view = mRegistry->view<ScriptComponent>();
    const std::vector<entt::entity> entities(view.begin(), view.end());
    for (auto entity : entities)
    {
        const auto& component = mRegistry->get<ScriptComponent>(entity);
        if (!component.enabled || mInstances.contains(entity)) continue;
        auto [it, inserted] = mInstances.emplace(entity, mRuntime.Create(entity, component));
        if (inserted && !it->second.failed) mRuntime.Call(it->second, "OnInit");
    }
}
void ScriptSystem::Update(entt::registry& registry, const FrameContext& context)
{
    ZoneScopedN("ScriptUpdate");
    if (!mPlaying || !mReady || mRegistry != &registry) return;
    mRuntime.SetInput(context.input, ImGuiBridge::WantsCaptureInput(), true);
    Sync();
    for (auto& [entity, instance] : mInstances)
        if (mRegistry->get<ScriptComponent>(entity).enabled) mRuntime.Call(instance, "OnUpdate", context.timer.DeltaTime());
    FlushDestructions();
    DeliverEvents();
    FlushDestructions();
}
void ScriptSystem::FixedUpdate(const FrameContext& context)
{
    ZoneScopedN("ScriptFixedUpdate");
    if (!mPlaying || !mReady || !mRegistry) return;
    // Edge events belong exclusively to OnUpdate: never repeat a press across substeps.
    mRuntime.SetInput(context.input, ImGuiBridge::WantsCaptureInput(), false);
    Sync();
    for (auto& [entity, instance] : mInstances)
        if (mRegistry->get<ScriptComponent>(entity).enabled) mRuntime.Call(instance, "OnFixedUpdate", context.physDT);
    FlushDestructions(); // All scripts finish before PhysicsSystem dispatches any workers.
}
void ScriptSystem::DeliverEvents()
{
    ZoneScopedN("ScriptEvents");
    auto events = mRuntime.TakeEvents();
    for (const auto& event : events)
    {
        if (!mRuntime.Valid(event.target)) continue;
        const auto* component = mRegistry->try_get<ScriptComponent>(event.target.entity);
        const auto it = mInstances.find(event.target.entity);
        if (component && component->enabled && it != mInstances.end() && !it->second.failed)
            mRuntime.Call(it->second, "OnEvent", 0, &event);
    }
}
void ScriptSystem::FlushDestructions()
{
    if (mStopping) return;
    auto commands = mRuntime.TakeDestructions();
    for (auto entity : commands) if (mRuntime.Valid(entity)) DestroyEntity(entity.entity);
}
void ScriptSystem::DestroyEntity(entt::entity entity)
{
    if (!mRegistry || !mRegistry->valid(entity)) return;
    RemoveInstance(*mRegistry, entity); // OnDestroy sees Transform/Rigidbody before EnTT erases them.
    mRegistry->destroy(entity);
}
const ScriptRuntime::Description& ScriptSystem::Describe(const std::string& path, bool reload)
{
    if (reload && mPlaying) throw std::logic_error("Reload is available only outside Play");
    auto it = mDescriptions.find(path);
    if (it == mDescriptions.end() || reload) it = mDescriptions.insert_or_assign(path, mRuntime.Describe(path)).first;
    return it->second;
}
std::string ScriptSystem::GetStatus(entt::entity entity) const
{
    if (!mRegistry || !mRegistry->valid(entity)) return "Entity removed";
    const auto* component = mRegistry->try_get<ScriptComponent>(entity);
    if (!component) return "No ScriptComponent";
    const auto it = mInstances.find(entity);
    if (it != mInstances.end() && it->second.failed) return "Error (disabled until next Play)";
    if (!mPlaying) return "Edit (callbacks stopped)";
    if (!mReady) return "Waiting for complete scene";
    if (!component->enabled) return "Disabled (state retained)";
    return it == mInstances.end() ? "Pending initialization" : "Running";
}
std::string ScriptSystem::GetError(entt::entity entity) const
{
    const auto it = mInstances.find(entity);
    if (it != mInstances.end() && !it->second.error.empty()) return it->second.error;
    const auto last = mLastErrors.find(entity);
    return last == mLastErrors.end() ? std::string{} : last->second;
}
