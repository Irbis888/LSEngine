#include "TerrainSystem.h"
#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>

TerrainSystem::TerrainSystem(JobSystem& jobs,IRenderAdapter& adapter,std::filesystem::path root)
    : mJobs(jobs),mAdapter(adapter),mRoot(std::move(root)) {}
TerrainSystem::~TerrainSystem() { Shutdown(); }
void TerrainSystem::Reset()
{
    if(mRunning)mRunning->cancel=true;
    for(auto& [e,s]:mEntries) if(s.pending)s.pending->cancel=true;
    mEntries.clear();
}
void TerrainSystem::Shutdown()
{
    Reset(); if(mTask) {mJobs.Wait(mTask);mTask={};} mRunning.reset();
}
bool TerrainSystem::IsBusy() const
{
    if(mRunning)return true;for(auto& [e,s]:mEntries)if(s.waiting||s.pending)return true;return false;
}
void TerrainSystem::Update(entt::registry& reg,const FrameContext&)
{
    ZoneScopedN("Terrain publish and schedule");
    for(auto it=mEntries.begin();it!=mEntries.end();)
        if(!reg.valid(it->first)||!reg.all_of<TerrainComponent,TransformComponent>(it->first))
        {if(it->second.pending)it->second.pending->cancel=true;it=mEntries.erase(it);}else ++it;
    for(auto e:reg.view<TerrainComponent,TransformComponent>())
    {
        auto& c=reg.get<TerrainComponent>(e);auto& s=mEntries[e];
        if(s.revision!=c.revision)
        {
            if(s.pending)s.pending->cancel=true;
            s.pending.reset();s.revision=c.revision;s.requested=c;s.waiting=true;s.status="Queued";
            try {Terrain::Validate(c);}catch(const std::exception& error){s.waiting=false;s.status=error.what();}
        }
    }
    if(mRunning&&mTask.IsComplete())
    {
        mJobs.Wait(mTask);
        for(auto& [e,s]:mEntries) if(s.pending==mRunning)
        {
            if(mRunning->cancel) s.pending.reset();
            else if(!mRunning->error.empty()) {s.status=mRunning->error;s.pending.reset();}
            else s.status="Uploading";
        }
        mRunning.reset();mTask={};
    }
    if(mRunning)return;
    size_t used=0;for(auto& [e,s]:mEntries){if(s.active)used+=s.active->Bytes();if(s.pending&&s.pending->resource)used+=s.pending->resource->Bytes();}
    for(auto& [e,s]:mEntries)if(s.waiting)
    {
        const auto estimate=Terrain::EstimateBytes(s.requested);
        // Leave up to 128 MiB for decoded layer images. This admission budget
        // includes old versions kept visible during rebuild and completed results.
        if(used+estimate+128*1024*1024>MemoryBudget){s.status="Waiting for terrain memory budget (remove another terrain)";continue;}
        s.waiting=false;s.pending=std::make_shared<BuildResult>();mRunning=s.pending;s.status="Building on CPU";
        auto result=mRunning;auto settings=s.requested;auto root=mRoot;
        mTask=mJobs.Submit([result,settings,root]
        {
            try {result->resource=Terrain::Build(settings,root,result->cancel);}
            catch(const std::exception& e){result->error=e.what();}
        },JobSystem::Priority::Low);
        break;
    }
}
void TerrainSystem::Draw(entt::registry& reg)
{
    ZoneScopedN("Terrain visibility and LOD");
    for(auto& [e,s]:mEntries)
    {
        if(!reg.valid(e)||!reg.all_of<TerrainComponent,TransformComponent>(e))continue;
        auto& c=reg.get<TerrainComponent>(e);const auto& t=reg.get<TransformComponent>(e);
        if(!Terrain::ValidTransform(t)){s.status="Terrain requires zero rotation and finite positive scale";continue;}
        if(s.pending&&s.pending!=mRunning&&s.pending->resource)
        {
            try
            {
                if(mAdapter.PrepareTerrain(s.pending->resource))
                {s.active=s.pending->resource;s.pending.reset();s.lod.assign(s.active->chunks.size(),0);s.status="Ready";}
            }
            catch(const std::exception& error){s.status=error.what();s.pending.reset();}
            catch(const DxException& error){s.status="Terrain GPU upload failed (HRESULT "+std::to_string(error.ErrorCode)+")";s.pending.reset();}
        }
        if(!s.active)continue;
        const auto start=std::chrono::steady_clock::now();s.stats={};s.stats.cpuBytes=s.active->Bytes();s.stats.buildMs=s.active->buildMs;
        s.stats.gpuBytes=mAdapter.TerrainGpuBytes();s.visible.clear();const auto camera=mAdapter.TerrainCameraPosition();
        for(uint32_t i=0;i<s.active->chunks.size();++i)
        {
            const auto& chunk=s.active->chunks[i];const auto lo=t.position+chunk.min*t.scale,hi=t.position+chunk.max*t.scale;
            // Horizontal distance to the real chunk footprint: high cliffs still get detail nearby.
            auto delta=glm::max(glm::max(lo-camera,camera-hi),glm::vec3(0));delta.y=0;
            s.lod[i]=Terrain::SelectLod(glm::length(delta),s.lod[i],s.active->settings);
            if(c.culling&&!mAdapter.TerrainVisible(lo,hi)){++s.stats.culled;continue;}
            s.visible.push_back({i,s.lod[i]});++s.stats.visible;++s.stats.draws;
            if(c.showBounds)++s.stats.draws;
            s.stats.triangles+=s.active->grids[s.lod[i]].indices.size()/3;
        }
        s.stats.selectMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        mAdapter.DrawTerrain(*s.active,t,c,s.visible);
    }
}
std::shared_ptr<const TerrainResource> TerrainSystem::GetResource(entt::entity e) const
{auto it=mEntries.find(e);return it==mEntries.end()?nullptr:it->second.active;}
TerrainStats TerrainSystem::GetStats(entt::entity e) const
{auto it=mEntries.find(e);return it==mEntries.end()?TerrainStats{}:it->second.stats;}
std::string TerrainSystem::GetStatus(entt::entity e) const
{auto it=mEntries.find(e);return it==mEntries.end()?"Waiting for preparation":it->second.status;}
bool TerrainSystem::TryGetHeight(entt::registry& reg,float x,float z,float& h) const
{
    bool found=false;for(auto& [e,s]:mEntries)if(s.active&&reg.valid(e)&&reg.all_of<TerrainComponent,TransformComponent>(e))
    {float value;if(s.active->TryGetHeight(reg.get<TransformComponent>(e),x,z,value)&&(!found||value>h)){found=true;h=value;}}return found;
}
bool TerrainSystem::TryGetNormal(entt::registry& reg,float x,float z,glm::vec3& n) const
{
    float best=-std::numeric_limits<float>::max();bool found=false;
    for(auto& [e,s]:mEntries)if(s.active&&reg.valid(e)&&reg.all_of<TerrainComponent,TransformComponent>(e))
    {float h;const auto& t=reg.get<TransformComponent>(e);if(s.active->TryGetHeight(t,x,z,h)&&h>best){best=h;found=s.active->TryGetNormal(t,x,z,n);}}return found;
}
void TerrainSystem::ResolveContacts(entt::registry& reg)
{
    ZoneScopedN("Terrain heightfield contacts");
    for(auto& [e,s]:mEntries)
    {
        if(!s.active||!reg.valid(e)||!reg.all_of<TerrainComponent,TransformComponent>(e)||!reg.get<TerrainComponent>(e).collision)continue;
        const auto& terrain=reg.get<TransformComponent>(e);const auto& settings=reg.get<TerrainComponent>(e);
        for(auto body:reg.view<TransformComponent,RigidbodyComponent,ColliderComponent>())
        {
            auto& t=reg.get<TransformComponent>(body);auto& b=reg.get<RigidbodyComponent>(body);const auto& c=reg.get<ColliderComponent>(body);
            if(!b.IsDynamic()||c.isTrigger)continue;
            // Iterate corrections over the affected cells as the normal can move the footprint.
            for(int pass=0;pass<4;++pass)
            {
                glm::vec3 normal;float depth;
                if(!Terrain::Contact(*s.active,terrain,MakeScaledAABB(t.position,t.scale,c),c.type,normal,depth))break;
                t.position+=normal*depth;PhysicsStats::AddCollision();
                const float vn=glm::dot(b.velocity,normal),inverseMass=b.InverseMass();
                if(vn<0){const float impulse=-(1+std::min(c.restitution,settings.restitution))*vn/inverseMass;b.velocity+=normal*impulse*inverseMass;}
                const auto nv=normal*glm::dot(b.velocity,normal);
                b.velocity=nv+(b.velocity-nv)*std::max(0.f,1-std::min(c.friction,settings.friction));
            }
        }
    }
}
