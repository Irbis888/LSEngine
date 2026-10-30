#pragma once
#include "Commons.h"
#include "ScriptComponent.h"
#include "SceneSerializer.h"
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <thread>

// One interpreter per Engine. All methods must run on its creating thread.
class ScriptRuntime
{
public:
    struct EntityRef { entt::entity entity = entt::null; uint64_t generation = 0; };
    struct Event
    {
        EntityRef target, sender;
        std::string name;
        ScriptParameters payload;
    };
    struct Instance
    {
        entt::entity entity = entt::null;
        std::string path;
        int reference = -2;
        bool failed = false;
        std::string error;
    };
    struct Description { ScriptParameters defaults; std::string error; };
    // Entity.Spawn reserves the ID at once; components are added at the next safe boundary.
    struct SpawnRequest
    {
        EntityRef entity;
        std::string path; // Canonical Prefabs/... path, for diagnostics.
        // Shared with the per-session prefab cache; ScriptSystem stores the first mesh ID in it.
        std::shared_ptr<SceneSerializer::EntityDescription> prefab;
        glm::vec3 position{};
        ScriptParameters overrides;
    };

    explicit ScriptRuntime(std::filesystem::path assetRoot);
    ~ScriptRuntime();
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;
    void Bind(entt::registry& registry);
    void Unbind(); // Invalidates references even if ECS IDs are reused after swap.
    void SetInput(const InputState& input, bool captured, bool allowEdges);
    Description Describe(const std::string& path);
    Instance Create(entt::entity entity, const ScriptComponent& component);
    bool Call(Instance& instance, const char* callback, float dt = 0, const Event* event = nullptr);
    void Release(Instance& instance);
    void ClearCommands();
    std::deque<Event> TakeEvents(); // Snapshot: newly sent events wait until next pass.
    std::vector<EntityRef> TakeDestructions();
    std::vector<SpawnRequest> TakeSpawns();
    bool Valid(EntityRef entity) const;
    EntityRef Reference(entt::entity entity) const;
    const std::filesystem::path& GetAssetRoot() const;
    void SetInstructionLimit(uint32_t limit);
    // Script text is read once per session and cached by canonical path; Bind clears the cache,
    // so every Play re-reads files. Hot reload installs validated text; the previous text is returned
    // (empty optional if none was cached) so a rejected version can be rolled back.
    std::optional<std::string> SetSource(const std::string& path, std::optional<std::string> source);
    void ForgetSource(const std::string& path);
    // Any thread: compiles text in a private temporary interpreter. Empty result = valid syntax.
    static std::string CheckSyntax(std::string_view source, const std::string& path);
    static std::string StripBom(std::string source);
    // Session prefab cache. Replacing an entry affects only later Entity.Spawn calls.
    std::vector<std::string> GetPrefabPaths() const;
    void SetPrefab(const std::string& path, std::shared_ptr<SceneSerializer::EntityDescription> prefab);
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
