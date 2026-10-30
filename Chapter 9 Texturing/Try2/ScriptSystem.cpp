#include "ScriptSystem.h"
#include "ImGuiBridge.h"
#include "AssetPaths.h"
#include <fstream>
#include <iostream>
#include <set>

namespace
{
    constexpr auto HotReloadInterval = std::chrono::milliseconds(250);
}

// One watcher pass. Paths are canonical and resolved on the main thread; the worker only
// touches the filesystem, its own copies and a private Lua state for validation.
struct ScriptSystem::HotReloadScan
{
    struct File
    {
        std::string path;           // Canonical asset path (Scripts/... or Prefabs/...).
        std::filesystem::path file; // Resolved native path.
        std::string tag;            // Prefab default tag (file stem).
        bool prefab = false;
        std::optional<FileStamp> known; // Last stamp seen; empty = first sighting, record only.
        // Results:
        bool exists = false, changed = false;
        FileStamp current;
        std::string source, error;
        std::shared_ptr<SceneSerializer::EntityDescription> description;
    };
    std::vector<File> files;
};

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
    if (mPlaying) { Sync(); FlushSpawns(); FlushDestructions(); }
}
void ScriptSystem::BeginPlay()
{
    if (!mRegistry || !mReady) throw std::logic_error("Cannot start scripts on a partially loaded scene");
    if (mPlaying) return;
    mRuntime.Bind(*mRegistry); // New session generation, even without a scene swap.
    mPlaying = true;
    mLastErrors.clear();
    Sync();
    FlushSpawns();
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
    DiscardSpawns();
    mRuntime.ClearCommands();
    while (!mInstances.empty())
    {
        const auto entity = mInstances.begin()->first;
        if (mRegistry && mRegistry->valid(entity)) RemoveInstance(*mRegistry, entity);
        else { mRuntime.Release(mInstances.begin()->second); mInstances.erase(mInstances.begin()); }
    }
    DiscardSpawns();
    mRuntime.ClearCommands(); // Discard commands/events emitted by OnDestroy during teardown.
    mInitialTransforms.clear();
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
        // First start in this session: remember where the object began, for hot reload reset.
        if (const auto* transform = mRegistry->try_get<TransformComponent>(entity)) mInitialTransforms.try_emplace(entity, *transform);
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
    // Objects spawned by OnInit of instances started just now are complete before their OnUpdate.
    FlushSpawns();
    FlushDestructions();
    for (auto& [entity, instance] : mInstances)
        if (mRegistry->get<ScriptComponent>(entity).enabled) mRuntime.Call(instance, "OnUpdate", context.timer.DeltaTime());
    FlushSpawns();
    FlushDestructions();
    DeliverEvents();
    FlushSpawns();
    FlushDestructions();
}
void ScriptSystem::FixedUpdate(const FrameContext& context)
{
    ZoneScopedN("ScriptFixedUpdate");
    if (!mPlaying || !mReady || !mRegistry) return;
    // Edge events belong exclusively to OnUpdate: never repeat a press across substeps.
    mRuntime.SetInput(context.input, ImGuiBridge::WantsCaptureInput(), false);
    Sync();
    FlushSpawns();
    FlushDestructions();
    for (auto& [entity, instance] : mInstances)
        if (mRegistry->get<ScriptComponent>(entity).enabled) mRuntime.Call(instance, "OnFixedUpdate", context.physDT);
    FlushSpawns();
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
// Spawned components appear here, never inside a callback. Their scripts start
// (OnInit) at the next Sync, i.e. the following fixed step or update.
void ScriptSystem::FlushSpawns()
{
    if (mStopping) return;
    for (auto& spawn : mRuntime.TakeSpawns())
    {
        if (!mRuntime.Valid(spawn.entity)) continue; // Destroyed again before the boundary.
        try
        {
            if (!mResources) throw std::logic_error("Entity.Spawn requires the engine's ResourceManager");
            auto description = *spawn.prefab;
            description.transform.position = spawn.position;
            for (auto& [name, value] : spawn.overrides) description.script->parameters[name] = std::move(value);
            SceneSerializer::Instantiate(*mRegistry, spawn.entity.entity, *mResources, description);
            // Later copies reuse this mesh/material, so repeated spawns do not grow resource tables.
            if (spawn.prefab->mesh && !spawn.prefab->mesh->existing)
                spawn.prefab->mesh->existing = mRegistry->get<MeshComponent>(spawn.entity.entity).meshID;
        }
        catch (const std::exception& error)
        {
            std::cerr << "[Lua] Spawn " << spawn.path << " failed: " << error.what() << std::endl;
            DestroyEntity(spawn.entity.entity);
        }
    }
}
void ScriptSystem::DiscardSpawns()
{
    // Reserved IDs have no components yet; drop them with the stopped session.
    for (const auto& spawn : mRuntime.TakeSpawns())
        if (mRegistry && mRuntime.Valid(spawn.entity)) mRegistry->destroy(spawn.entity.entity);
}
void ScriptSystem::DestroyEntity(entt::entity entity)
{
    if (!mRegistry || !mRegistry->valid(entity)) return;
    mInitialTransforms.erase(entity);
    RemoveInstance(*mRegistry, entity); // OnDestroy sees Transform/Rigidbody before EnTT erases them.
    mRegistry->destroy(entity);
}
const ScriptRuntime::Description& ScriptSystem::Describe(const std::string& path, bool reload)
{
    if (reload && mPlaying) throw std::logic_error("Reload is available only outside Play");
    if (reload) mRuntime.ForgetSource(path); // Explicit Reload always reads the file again.
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

void ScriptSystem::PumpHotReload()
{
    if (!mJobs || !mJobs->IsInitialized()) return;
    if (mScanTask)
    {
        if (!mScanTask.IsComplete()) return; // Never block a frame on the watcher.
        mJobs->Wait(mScanTask);              // Completed: returns at once.
        mScanTask = {};
        auto scan = std::move(mScan);
        ApplyHotReload(*scan);
    }
    if (!mHotReload) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < mNextScan) return;
    mNextScan = now + HotReloadInterval;
    SubmitHotReloadScan();
}
void ScriptSystem::StopHotReload()
{
    if (mScanTask && mJobs && mJobs->IsInitialized()) mJobs->Wait(mScanTask);
    mScanTask = {};
    mScan.reset();
}
void ScriptSystem::SubmitHotReloadScan()
{
    // Watched set: scripts used by the scene or shown in the inspector, prefabs of this session.
    std::set<std::string> scripts;
    if (mRegistry)
        for (auto entity : mRegistry->view<ScriptComponent>())
            if (const auto& path = mRegistry->get<ScriptComponent>(entity).path; !path.empty()) scripts.insert(path);
    for (const auto& [path, description] : mDescriptions) scripts.insert(path);
    const auto& root = mRuntime.GetAssetRoot();
    auto scan = std::make_shared<HotReloadScan>();
    std::set<std::string> seen;
    const auto add = [&](const std::string& path, bool prefab)
    {
        const auto key = (prefab ? "prefab:" : "script:") + path;
        if (!seen.insert(key).second) return;
        HotReloadScan::File file;
        file.path = path; file.prefab = prefab; file.file = AssetPaths::Resolve(root, path);
        if (prefab) file.tag = AssetPaths::ToUtf8(AssetPaths::FromUtf8(path).stem());
        if (const auto it = mStamps.find(key); it != mStamps.end()) file.known = it->second;
        scan->files.push_back(std::move(file));
    };
    for (const auto& raw : scripts)
    {
        try { add(AssetPaths::ScriptPath(root, raw), false); }
        catch (const std::exception&) {} // Invalid path: Load/Describe already report it.
    }
    for (const auto& path : mRuntime.GetPrefabPaths()) add(path, true);
    if (scan->files.empty()) { mStamps.clear(); return; }
    mScan = scan;
    mScanTask = mJobs->Submit([scan]
    {
        ZoneScopedN("ScriptHotReloadScan");
        for (auto& file : scan->files)
        {
            try
            {
                std::error_code error;
                const auto time = std::filesystem::last_write_time(file.file, error);
                if (error) continue; // Missing or locked by an editor mid-save: try again next pass.
                const auto size = std::filesystem::file_size(file.file, error);
                if (error) continue;
                file.exists = true;
                file.current = { time, size };
                if (!file.known || *file.known == file.current) continue;
                file.changed = true;
                if (file.prefab)
                {
                    file.description = std::make_shared<SceneSerializer::EntityDescription>(
                        SceneSerializer::ReadPrefab(file.file, file.tag));
                    continue;
                }
                if (size > 1024 * 1024) throw std::runtime_error("Lua file exceeds 1 MiB");
                std::ifstream in(file.file, std::ios::binary);
                if (!in) throw std::runtime_error("Cannot open Lua file: " + file.path);
                file.source = ScriptRuntime::StripBom({ std::istreambuf_iterator<char>(in), {} });
                file.error = ScriptRuntime::CheckSyntax(file.source, file.path);
            }
            catch (const std::exception& error) { file.error = error.what(); }
        }
    }, JobSystem::Priority::Low);
}
void ScriptSystem::ApplyHotReload(HotReloadScan& scan)
{
    ZoneScopedN("ScriptHotReloadApply");
    std::map<std::string, FileStamp> stamps; // Paths that left the watched set are forgotten.
    for (auto& file : scan.files)
    {
        const auto key = (file.prefab ? "prefab:" : "script:") + file.path;
        if (!file.exists)
        {
            if (const auto it = mStamps.find(key); it != mStamps.end()) stamps.emplace(key, it->second);
            continue;
        }
        stamps[key] = file.current;
        if (!file.changed) continue;
        if (!file.error.empty())
        {
            Notify("Hot reload rejected " + file.path + " (running version kept): " + file.error, true);
            continue;
        }
        if (file.prefab)
        {
            mRuntime.SetPrefab(file.path, std::move(file.description));
            ++mHotReloadCount;
            Notify("Hot reload: prefab " + file.path + " (applies to the next spawn)", false);
            continue;
        }
        ReloadScript(file.path, std::move(file.source));
    }
    mStamps = std::move(stamps);
}
void ScriptSystem::ReloadScript(const std::string& path, std::string source)
{
    const auto& root = mRuntime.GetAssetRoot();
    const auto canonical = [&](const std::string& raw)
    {
        try { return AssetPaths::ScriptPath(root, raw); }
        catch (const std::exception&) { return std::string{}; }
    };
    // Top-level code runs (declarations only, no scene access) before anything is replaced.
    auto previous = mRuntime.SetSource(path, std::move(source));
    auto description = mRuntime.Describe(path);
    const bool live = mPlaying && mReady && mRegistry;
    if (!description.error.empty())
    {
        if (live) mRuntime.SetSource(path, std::move(previous)); // Later spawns keep the running text.
        else for (auto& [raw, cached] : mDescriptions) if (canonical(raw) == path) cached = description; // Inspector shows it.
        Notify("Hot reload rejected " + path + " (running version kept): " + description.error, true);
        return;
    }
    for (auto& [raw, cached] : mDescriptions) if (canonical(raw) == path) cached = description;
    ++mHotReloadCount;
    if (!live) { Notify("Hot reload: " + path + " (defaults refreshed; applies at Play)", false); return; }
    // L1 reset: OnDestroy and release now (outside callbacks); Sync recreates each instance with
    // fresh Lua state and the new text, then calls OnInit, at the next update or fixed step.
    std::map<std::string, std::string> paths;
    std::vector<entt::entity> targets;
    for (const auto& [entity, instance] : mInstances)
    {
        auto [it, inserted] = paths.try_emplace(instance.path);
        if (inserted) it->second = canonical(instance.path);
        if (it->second == path) targets.push_back(entity);
    }
    for (auto entity : targets)
    {
        if (!mRegistry->valid(entity)) continue;
        RemoveInstance(*mRegistry, entity);
        mLastErrors.erase(entity);
        // The object restarts where it began this session (scene or spawn transform), at rest.
        const auto initial = mInitialTransforms.find(entity);
        if (auto* transform = mRegistry->try_get<TransformComponent>(entity); transform && initial != mInitialTransforms.end())
            *transform = initial->second;
        if (auto* body = mRegistry->try_get<RigidbodyComponent>(entity)) body->velocity = glm::vec3(0);
    }
    Notify("Hot reload: " + path + " - " + std::to_string(targets.size()) + " instance(s) restarted with reset state", false);
}
void ScriptSystem::Notify(const std::string& message, bool error)
{
    (error ? std::cerr : std::cout) << "[Lua] " << message << std::endl;
    mHotReloadNotice = message;
}
