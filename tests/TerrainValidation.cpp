#include "TerrainValidation.h"
#include "Engine.h"
#include "D3DRenderAdapter.h"
#include "AssetPaths.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorSceneIO.h"
#include <d3d12sdklayers.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <psapi.h>
#pragma comment(lib,"Psapi.lib")

namespace
{
    void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
    entt::entity Find(entt::registry& reg,const char* name)
    {for(auto e:reg.view<TagComponent>())if(reg.get<TagComponent>(e).tag==name)return e;return entt::null;}
}
void TerrainValidation::Math(const std::filesystem::path& root)
{
    std::atomic<bool> cancel=false;TerrainComponent s;s.source="flat";s.samplesX=s.samplesZ=9;s.chunkCells=8;s.lodCount=4;s.sizeX=s.sizeZ=8;s.heightScale=100;s.heightOffset=2;
    auto r=Terrain::Build(s,root,cancel);TransformComponent t{{10,20,30},{0,0,0},{2,3,4}};float h;glm::vec3 n;
    Check(r->TryGetHeight(t,14,34,h)&&h==26,"Height transform formula mismatch");
    Check(!r->TryGetHeight(t,9,34,h)&&!r->TryGetHeight(t,14,63,h),"Outside terrain must have no height");
    Check(r->TryGetNormal(t,14,34,n)&&glm::length(n-glm::vec3(0,1,0))<1e-5f,"Flat terrain normal is incorrect");
    t.rotation.x=.1f;Check(!r->TryGetHeight(t,14,34,h),"Rotated terrain was accepted");t.rotation={0,0,0};
    Check(Terrain::SelectLod(110,0,s)==0&&Terrain::SelectLod(116,0,s)==1&&Terrain::SelectLod(90,1,s)==1&&Terrain::SelectLod(84,1,s)==0,"LOD hysteresis is incorrect");
    for(size_t i=1;i<r->grids.size();++i)Check(r->grids[i].indices.size()<r->grids[i-1].indices.size(),"LOD does not reduce triangle count");
    for(auto& grid:r->grids)for(auto index:grid.indices)Check(index<grid.vertices.size(),"Grid index out of bounds");
    for(auto& c:r->chunks)Check(c.min.y<=s.heightOffset-c.skirt&&c.max.y>=s.heightOffset,"Skirts excluded from bounds");
    AABB box{{14,26.5f,34},{1,1,1}};float depth;
    Check(Terrain::Contact(*r,t,box,ColliderType::AABB,n,depth)&&std::abs(depth-.5f)<1e-5f,"AABB/heightfield contact mismatch");
    Check(Terrain::Contact(*r,t,box,ColliderType::Sphere,n,depth)&&std::abs(depth-.5f)<1e-5f,"Sphere/heightfield contact mismatch");
    // Peak inside a large box, away from its centre and its four corners.
    r->heights[2*9+2]=8;AABB large{{4,4,4},{3,1,3}};TransformComponent identity{{0,0,0},{0,0,0},{1,1,1}};
    Check(Terrain::Contact(*r,identity,large,ColliderType::AABB,n,depth)&&depth>0,"Large AABB missed an interior heightfield peak");
    for(uint32_t z=0;z<9;++z)for(uint32_t x=0;x<9;++x)r->heights[z*9+x]=2+x+z*.5f;
    Check(r->TryGetHeight(t,14,34,h)&&std::abs(h-33.5f)<1e-5f,"Sloped transformed terrain height mismatch");
    Check(r->TryGetNormal(t,14,34,n)&&glm::length(n-glm::normalize(glm::vec3(-1.5f,1,-.375f)))<1e-5f,"Nonuniform terrain normal transform mismatch");
    const auto file=output/L"height-16bit-тест.png";
    std::filesystem::copy_file(root/"../../tests/fixtures/Terrain16.png",file,std::filesystem::copy_options::overwrite_existing);
    s.source="heightmap";s.heightmap=AssetPaths::ToUtf8(file);s.heightOffset=0;s.heightScale=1;
    r=Terrain::Build(s,root,cancel);Check(std::abs(r->heights[1]-1025/65535.f)<1e-7f&&r->heights[1]!=r->heights[0],"16-bit height precision was lost");
    const auto broken=output/"corrupt.png";{std::ofstream f(broken);f<<"not a PNG";}
    s.heightmap=AssetPaths::ToUtf8(broken);bool corruptRejected=false;try{Terrain::Build(s,root,cancel);}catch(...){corruptRejected=true;}
    Check(corruptRejected,"Corrupt heightmap was accepted");
    Check(Terrain::FromJson(Terrain::ToJson(s)).samplesX==s.samplesX,"Terrain JSON round-trip failed");
    s.source="procedural";auto a=Terrain::Build(s,root,cancel),b=Terrain::Build(s,root,cancel);Check(a->heights==b->heights,"Procedural seed is not reproducible");
    cancel=true;bool rejected=false;try{Terrain::Build(s,root,cancel);}catch(...){rejected=true;}Check(rejected,"Cancelled build published data");
    std::cout<<"TERRAIN_MATH_PASS: transforms, normals, footprint contacts, 16-bit/Unicode, seed, bounds, indices, hysteresis, cancellation\n";
}
bool TerrainValidation::Tick(Engine& engine,const FrameContext& context,D3DRenderAdapter& renderer)
{
    if(readback)
    {
        renderer.FlushCommandQueue(); // Only the opt-in capture path waits for readback.
        const auto w=int(footprint.Footprint.Width),h=int(footprint.Footprint.Height);
        std::vector<uint8_t> bmp(54+size_t(w)*h*4);bmp[0]='B';bmp[1]='M';
        auto put=[&](size_t at,int value){memcpy(bmp.data()+at,&value,4);};
        put(2,int(bmp.size()));put(10,54);put(14,40);put(18,w);put(22,-h);bmp[26]=1;bmp[28]=32;put(34,w*h*4);
        uint8_t* pixels=nullptr;readback->Map(0,nullptr,reinterpret_cast<void**>(&pixels));
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        {auto src=pixels+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch+x*4;auto dst=bmp.data()+54+(size_t(y)*w+x)*4;dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=255;}
        readback->Unmap(0,nullptr);readback.Reset();std::ofstream file(output/captureName,std::ios::binary);file.write(reinterpret_cast<char*>(bmp.data()),bmp.size());
        std::cout<<"TERRAIN_CAPTURE "<<AssetPaths::ToUtf8(output/captureName)<<std::endl;
    }
    auto& reg=engine.GetRegistry();auto& system=engine.GetTerrain();std::string error;
    peakGpu=std::max(peakGpu,renderer.TerrainGpuBytes());
    if(output.empty())
    {
        output=std::filesystem::temp_directory_path()/("LSEngineTerrainSmoke-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directories(output);
        Math(engine.GetAssetRoot());
    }
    if(engine.IsSceneLoading())return false;
    Check(engine.GetSceneLoadProgress().error.empty(),engine.GetSceneLoadProgress().error.c_str());
    if(stage==0)
    {
        terrain=Find(reg,"Large Terrain");Check(terrain!=entt::null,"Terrain demo entity missing");
        if(!system.GetResource(terrain)){loadingMax=std::max(loadingMax,context.timer.DeltaTime()*1000.);return false;}
        const auto stats=system.GetStats(terrain);Check(stats.visible>0&&stats.culled>0&&stats.visible+stats.culled==system.GetResource(terrain)->chunks.size(),"Large terrain culling is incorrect");
        const auto cam=Find(reg,"Camera");const auto& transform=reg.get<TransformComponent>(cam);
        const glm::vec3 forward(0,-std::sin(transform.rotation.x),std::cos(transform.rotation.x));
        const auto front=transform.position+forward*50.f,back=transform.position-forward*50.f;
        Check(renderer.TerrainVisible(front-glm::vec3(1),front+glm::vec3(1))&&!renderer.TerrainVisible(back-glm::vec3(1),back+glm::vec3(1)),"Frustum front/back planes are incorrect");
        Check(renderer.TerrainVisible(transform.position-glm::vec3(1),transform.position+glm::vec3(1)),"Frustum incorrectly culled a near-plane straddling box");
        Check(stats.triangles<size_t(system.GetResource(terrain)->settings.samplesX-1)*(system.GetResource(terrain)->settings.samplesZ-1)*2,"Geometric LOD did not reduce rendered triangle count");
        const auto ms=context.timer.DeltaTime()*1000.;largeTotal+=ms;largeMax=std::max(largeMax,ms);
        if(++largeFrames<60)return false;
        std::cout<<"TERRAIN_INITIAL build_ms="<<stats.buildMs<<" cpu_bytes="<<stats.cpuBytes<<" gpu_bytes="<<renderer.TerrainGpuBytes()<<" visible="<<stats.visible<<" culled="<<stats.culled<<" triangles="<<stats.triangles<<" large_mean_frame_ms="<<largeTotal/largeFrames<<" large_max_frame_ms="<<largeMax<<" loading_max_frame_ms="<<loadingMax<<std::endl;
        reg.get<TerrainComponent>(terrain).culling=false;stage=1;frames=0;return false;
    }
    if(stage==1)
    {
        if(++frames<3)return false;Check(system.GetStats(terrain).visible==system.GetResource(terrain)->chunks.size(),"Culling toggle failed");
        auto& c=reg.get<TerrainComponent>(terrain);c.culling=true;c.showBounds=true;c.showLod=true;c.wireframe=true;stage=2;frames=0;return false;
    }
    if(stage==2)
    {
        if(++frames<5)return false;auto& c=reg.get<TerrainComponent>(terrain);c.showBounds=c.showLod=c.wireframe=false;
        if(!engine.SaveScene(AssetPaths::ToUtf8(output/"LargeScene.json"),error))throw std::runtime_error(error);
        previous=system.GetResource(terrain);c.source="procedural";c.samplesX=c.samplesZ=257;c.seed++;++c.revision;stage=3;return false;
    }
    if(stage==3)
    {
        if(system.IsBusy()||system.GetResource(terrain)==previous)return false;
        Check(system.GetResource(terrain)->settings.samplesX==257,"Rebuild was not published");previous=system.GetResource(terrain);
        auto& c=reg.get<TerrainComponent>(terrain);c.source="heightmap";c.heightmap=AssetPaths::ToUtf8(output/"missing.png");++c.revision;stage=4;return false;
    }
    if(stage==4)
    {
        if(system.IsBusy())return false;Check(system.GetResource(terrain)==previous&&system.GetStatus(terrain)!="Ready","Failed build discarded previous terrain");
        auto& c=reg.get<TerrainComponent>(terrain);c.source="procedural";c.samplesX=c.samplesZ=2049;++c.revision;stage=5;return false;
    }
    if(stage==5)
    {
        Check(engine.LoadScene("Scenes/LuaDemo.json",error),"Scene switch during terrain build failed");previous.reset();stage=6;return false;
    }
    if(stage==6)
    {
        if(system.IsBusy())return false;Check(reg.view<TerrainComponent>().size()==0,"Old terrain survived scene swap");
        Check(engine.LoadScene(AssetPaths::ToUtf8(output/"LargeScene.json"),error),"Saved terrain scene could not be loaded");stage=7;return false;
    }
    if(stage==7)
    {
        terrain=Find(reg,"Large Terrain");if(terrain==entt::null||!system.GetResource(terrain)||system.IsBusy())return false;
        auto& c=reg.get<TerrainComponent>(terrain);c.source="procedural";c.samplesX=c.samplesZ=257;c.seed++;++c.revision;stage=8;return false;
    }
    if(stage==8)
    {
        if(system.IsBusy()||system.GetResource(terrain)->settings.samplesX!=257)return false;
        if(++cycles<6){auto& c=reg.get<TerrainComponent>(terrain);++c.seed;++c.revision;return false;}
        frames=0;stage=9;return false;
    }
    if(stage==9)
    {
        if(++frames<20)return false;Check(renderer.TerrainGpuBytes()<8*1024*1024,"Rebuilds retained old terrain GPU resources");
        std::cout<<"TERRAIN_REBUILD cycles="<<cycles<<" cpu_bytes="<<system.GetResource(terrain)->Bytes()<<" gpu_bytes="<<renderer.TerrainGpuBytes()<<std::endl;
        auto* editor=Editor_GetContext();Check(editor!=nullptr,"Editor context missing");EditorSceneIO::BeginPlay(*editor);Check(engine.IsPlaying(),"Terrain Play failed");
        frames=0;stage=10;return false;
    }
    if(stage==10)
    {
        FrameContext step=context;step.physDT=1.f/60;
        for(int i=0;i<10;++i)engine.PhysicsUpdate(step);
        if(++frames<120)return false;
        size_t checked=0;for(auto e:reg.view<TransformComponent,RigidbodyComponent,ColliderComponent>())
        {
            auto& t=reg.get<TransformComponent>(e);const auto& collider=reg.get<ColliderComponent>(e);float h;
            if(system.TryGetHeight(reg,t.position.x,t.position.z,h))
            {const auto box=MakeScaledAABB(t.position,t.scale,collider);Check(box.Max().y>=h-.1f,"Body fell through terrain");++checked;}
        }
        Check(checked>=10,"Terrain physics bodies missing");EditorSceneIO::EndPlay(*Editor_GetContext());stage=11;return false;
    }
    if(stage==11)
    {
        terrain=Find(reg,"Large Terrain");if(terrain==entt::null||!system.GetResource(terrain)||system.IsBusy())return false;
        Check(!engine.IsPlaying(),"Terrain Stop failed");reg.destroy(terrain);frames=0;stage=12;return false;
    }
    if(stage==12)
    {
        if(++frames<20)return false;Check(renderer.TerrainGpuBytes()==0,"Deleted terrain retained GPU resources");
        auto e=reg.create();reg.emplace<TagComponent>(e,"Terrain recreated");reg.emplace<TransformComponent>(e,glm::vec3(0),glm::vec3(0),glm::vec3(1));
        TerrainComponent c;c.samplesX=c.samplesZ=129;c.chunkCells=32;reg.emplace<TerrainComponent>(e,c);terrain=e;frames=0;stage=13;return false;
    }
    if(stage==13)
    {
        if(!system.GetResource(terrain)||system.IsBusy())return false;
        const double ms=context.timer.DeltaTime()*1000.;maxFrame=std::max(maxFrame,ms);totalFrames+=ms;++frameCount;
        if(++frames<60)return false;
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
        if(SUCCEEDED(renderer.GetImGuiBindings().Device->QueryInterface(IID_PPV_ARGS(&queue))))
        {
            for(UINT64 i=0;i<queue->GetNumStoredMessagesAllowedByRetrievalFilter();++i)
            {
                SIZE_T bytes=0;queue->GetMessage(i,nullptr,&bytes);std::vector<uint8_t> storage(bytes);auto* msg=reinterpret_cast<D3D12_MESSAGE*>(storage.data());queue->GetMessage(i,msg,&bytes);
                if(msg->Severity==D3D12_MESSAGE_SEVERITY_ERROR||msg->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)throw std::runtime_error(msg->pDescription);
            }
        }
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
        std::cout<<"TERRAIN_SMOKE_PASS: actual Application/D3D12/editor; rebuild/error/cancel/switch/save/load/Play/Stop/delete/recreate; small_mean_ms="<<totalFrames/frameCount<<" small_max_ms="<<maxFrame<<" peak_terrain_gpu_bytes="<<peakGpu<<" process_peak_working_set="<<memory.PeakWorkingSetSize<<std::endl;
        return true;
    }
    return false;
}
void TerrainValidation::AfterDraw(D3DRenderAdapter& renderer)
{
    if(readback||!((stage==1&&!capturedNormal)||(stage==2&&!capturedWire)))return;
    if(stage==1){capturedNormal=true;captureName="large-terrain.bmp";}else{capturedWire=true;captureName="lod-wire-bounds.bmp";}
    auto bindings=renderer.GetImGuiBindings();auto desc=renderer.CurrentBackBuffer()->GetDesc();UINT64 bytes;
    bindings.Device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_READBACK);const auto buffer=CD3DX12_RESOURCE_DESC::Buffer(bytes);
    ThrowIfFailed(bindings.Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(renderer.CurrentBackBuffer(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    bindings.CommandList->ResourceBarrier(1,&barrier);CD3DX12_TEXTURE_COPY_LOCATION src(renderer.CurrentBackBuffer(),0),dst(readback.Get(),footprint);
    bindings.CommandList->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    barrier=CD3DX12_RESOURCE_BARRIER::Transition(renderer.CurrentBackBuffer(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);bindings.CommandList->ResourceBarrier(1,&barrier);
}
