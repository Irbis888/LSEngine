#pragma once
#include "Terrain.h"
#include "JobSystem.h"

// Specialized asset manager: isolated versioned CPU results, one admitted build
// reserves worker capacity for physics; main thread alone publishes/uploads.
class TerrainSystem : public ISystem
{
public:
    TerrainSystem(JobSystem& jobs, IRenderAdapter& adapter, std::filesystem::path root);
    ~TerrainSystem();
    void Update(entt::registry&, const FrameContext&) override;
    void Draw(entt::registry&);
    void Reset();
    void Shutdown();
    void ResolveContacts(entt::registry&);
    std::shared_ptr<const TerrainResource> GetResource(entt::entity) const;
    TerrainStats GetStats(entt::entity) const;
    std::string GetStatus(entt::entity) const;
    bool IsBusy() const;
    bool TryGetHeight(entt::registry&,float x,float z,float& h) const;
    bool TryGetNormal(entt::registry&,float x,float z,glm::vec3& n) const;
private:
    struct BuildResult
    {
        std::atomic<bool> cancel{false};
        std::shared_ptr<TerrainResource> resource;
        std::string error;
    };
    struct Entry
    {
        uint64_t revision = 0;
        TerrainComponent requested;
        bool waiting = false;
        std::shared_ptr<const TerrainResource> active;
        std::shared_ptr<BuildResult> pending;
        std::string status;
        TerrainStats stats;
        std::vector<uint32_t> lod;
        std::vector<TerrainDrawChunk> visible;
    };
    JobSystem& mJobs;
    IRenderAdapter& mAdapter;
    std::filesystem::path mRoot;
    std::unordered_map<entt::entity,Entry> mEntries;
    std::shared_ptr<BuildResult> mRunning;
    JobSystem::TaskHandle mTask;
    static constexpr size_t MemoryBudget = 384*1024*1024;
};
