#include "D3DRenderAdapter.h"
#include "Terrain.h"
#include <algorithm>
#include <chrono>

void D3DRenderAdapter::BuildTerrainPipeline()
{
    CD3DX12_DESCRIPTOR_RANGE range;range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,10,0);
    CD3DX12_ROOT_PARAMETER params[4];params[0].InitAsDescriptorTable(1,&range);
    params[1].InitAsConstantBufferView(1);params[2].InitAsConstants(48,0);
    params[3].InitAsConstants(1, 3, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    auto samplers=GetStaticSamplers();CD3DX12_ROOT_SIGNATURE_DESC desc(4,params,UINT(samplers.size()),samplers.data(),D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> blob,error;
    const HRESULT hr=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);
    if(error)OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));ThrowIfFailed(hr);
    ThrowIfFailed(md3dDevice->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&mRootSignatures["terrain"])));
    auto vs=d3dUtils::CompileShader(L"Shaders/Terrain.hlsl",nullptr,"VSTerrain","vs_5_1");
    auto ps=d3dUtils::CompileShader(L"Shaders/Terrain.hlsl",nullptr,"PSTerrain","ps_5_1");
    auto bvs=d3dUtils::CompileShader(L"Shaders/Terrain.hlsl",nullptr,"VSBounds","vs_5_1");
    auto bps=d3dUtils::CompileShader(L"Shaders/Terrain.hlsl",nullptr,"PSBounds","ps_5_1");
    if(!vs||!ps||!bvs||!bps)throw std::runtime_error("Terrain shader compilation failed");
    D3D12_INPUT_ELEMENT_DESC input={"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.InputLayout={&input,1};p.pRootSignature=mRootSignatures["terrain"].Get();
    p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    p.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    p.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);p.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    p.SampleMask=UINT_MAX;p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=mBackBufferFormat;p.DSVFormat=mDepthStencilFormat;p.SampleDesc.Count=1;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&mPSOs["terrain"])));
    p.RasterizerState.FillMode=D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&mPSOs["terrainWire"])));
    const D3D_SHADER_MACRO editorDefines[] = {{"EDITOR_VIEWPORT", "1"}, {nullptr, nullptr}};
    auto editorPixelShader = d3dUtils::CompileShader(L"Shaders/Terrain.hlsl", editorDefines, "PSTerrain", "ps_5_1");
    p.PS = {editorPixelShader->GetBufferPointer(), editorPixelShader->GetBufferSize()};
    p.NumRenderTargets = 2;
    p.RTVFormats[1] = DXGI_FORMAT_R32_UINT;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&mPSOs["terrainWireEditor"])));
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&mPSOs["terrainEditor"])));
    p.NumRenderTargets = 1;
    p.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
    p.InputLayout={};p.VS={bvs->GetBufferPointer(),bvs->GetBufferSize()};p.PS={bps->GetBufferPointer(),bps->GetBufferSize()};
    p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&mPSOs["terrainBounds"])));
    p.NumRenderTargets = 2;
    p.RTVFormats[1] = DXGI_FORMAT_R32_UINT;
    p.BlendState.IndependentBlendEnable = TRUE;
    p.BlendState.RenderTarget[1].RenderTargetWriteMask = 0;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&mPSOs["terrainBoundsEditor"])));

}
void D3DRenderAdapter::CollectTerrainResources()
{
    mTerrainUploadBytes=0;mTerrainUploadMs=0;
    const auto completed=mFence->GetCompletedValue();
    for(auto it=mTerrainGPU.begin();it!=mTerrainGPU.end();)
    {
        auto& gpu=*it->second;
        if(gpu.fence&&completed>=gpu.fence){gpu.staging.clear();gpu.fence=0;}
        if(gpu.owner.expired())
        {
            if(!gpu.retireFence)gpu.retireFence=mCurrentFence+1;
            if(completed>=gpu.retireFence){mFreeTerrainDescriptors.push_back(gpu.descriptors);it=mTerrainGPU.erase(it);continue;}
        }
        ++it;
    }
}
size_t D3DRenderAdapter::TerrainGpuBytes() const {size_t n=0;for(auto& [r,g]:mTerrainGPU)n+=g->bytes;return n;}
bool D3DRenderAdapter::PrepareTerrain(const std::shared_ptr<const TerrainResource>& r)
{
    ZoneScopedN("Terrain budgeted GPU upload");
    auto found=mTerrainGPU.find(r.get());
    // An expired cache key may be reused by a fresh CPU allocation; never use its old data.
    if(found!=mTerrainGPU.end()&&found->second->owner.expired()) return false;
    if(found==mTerrainGPU.end())
    {
        auto gpu=std::make_unique<TerrainGPU>();gpu->owner=r;
        if(!mFreeTerrainDescriptors.empty()){gpu->descriptors=mFreeTerrainDescriptors.back();mFreeTerrainDescriptors.pop_back();}
        else {if(mNextCbvSrvIndex+10>mCbvSrvUavDescriptorCount)throw std::runtime_error("Terrain descriptor heap exhausted");gpu->descriptors=mNextCbvSrvIndex;mNextCbvSrvIndex+=10;}
        found=mTerrainGPU.emplace(r.get(),std::move(gpu)).first;
    }
    auto& g=*found->second;if(g.ready)return true;
    const auto start=std::chrono::steady_clock::now();
    auto elapsed=[&]{return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
    const auto upload=[&](UINT slot,uint32_t w,uint32_t h,DXGI_FORMAT format,uint32_t mips,const std::vector<ImageSubresource>& subs,const void* pixels)
    {
        const auto desc=CD3DX12_RESOURCE_DESC::Tex2D(format,w,h,1,UINT16(mips));
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT),uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
        ComPtr<ID3D12Resource> texture,staging;
        ThrowIfFailed(md3dDevice->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)));
        const auto uploadDesc=CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(texture.Get(),0,UINT(subs.size())));
        ThrowIfFailed(md3dDevice->CreateCommittedResource(&uploadHeap,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&staging)));
        std::vector<D3D12_SUBRESOURCE_DATA> source;for(auto& s:subs)source.push_back({static_cast<const uint8_t*>(pixels)+s.offset,LONG_PTR(s.rowPitch),LONG_PTR(s.slicePitch)});
        if(!UpdateSubresources(mCommandList.Get(),texture.Get(),staging.Get(),0,0,UINT(source.size()),source.data()))throw std::runtime_error("Terrain texture upload failed");
        const auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        mCommandList->ResourceBarrier(1,&barrier);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=mips;
        const CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(),g.descriptors+slot,mCbvSrvUavDescriptorSize);
        md3dDevice->CreateShaderResourceView(texture.Get(),&srv,handle);
        g.textures.push_back(texture);g.staging.push_back(staging);g.fence=mCurrentFence+1;
        g.bytes+=md3dDevice->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;mTerrainUploadBytes+=size_t(uploadDesc.Width);
    };
    while(g.nextImage<10)
    {
        // Soft time/byte budget; a single image always progresses. At most the
        // configured 32 MiB layer limit or one validated heightfield is admitted.
        if(mTerrainUploadBytes&&(mTerrainUploadBytes>=mUploadBudget.maxBytes||mTerrainUploadMs+elapsed()>=mUploadBudget.maxMilliseconds))break;
        const auto slot=g.nextImage;
        if(slot<2)
        {
            const uint32_t w=r->settings.samplesX,h=r->settings.samplesZ;
            upload(slot,w,h,slot==0?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM,1,{{0,size_t(w)*4,size_t(w)*h*4}},slot==0?static_cast<const void*>(r->heights.data()):r->weights.data());
        }
        else
        {
            const auto& image=r->images[slot-2];
            if(!image.pixels.empty())upload(slot,image.width,image.height,image.format,image.mipLevels,image.subresources,image.pixels.data());
            else {const uint8_t white[4]={255,255,255,255},normal[4]={128,128,255,255};upload(slot,1,1,DXGI_FORMAT_R8G8B8A8_UNORM,1,{{0,4,4}},slot%2?normal:white);}
        }
        ++g.nextImage;
    }
    if(g.nextImage==10&&g.vertices.empty())
    {
        for(auto& grid:r->grids)
        {
            ComPtr<ID3D12Resource> vbUpload,ibUpload;
            g.vertices.push_back(d3dUtils::CreateDefaultBuffer(md3dDevice.Get(),mCommandList.Get(),grid.vertices.data(),grid.vertices.size()*sizeof(glm::vec3),vbUpload));
            g.indices.push_back(d3dUtils::CreateDefaultBuffer(md3dDevice.Get(),mCommandList.Get(),grid.indices.data(),grid.indices.size()*sizeof(uint32_t),ibUpload));
            g.staging.push_back(vbUpload);g.staging.push_back(ibUpload);g.indexCounts.push_back(UINT(grid.indices.size()));g.vertexBytes.push_back(UINT(grid.vertices.size()*sizeof(glm::vec3)));
            g.bytes+=grid.vertices.size()*sizeof(glm::vec3)+grid.indices.size()*sizeof(uint32_t);
        }
        g.fence=mCurrentFence+1;g.ready=true;
    }
    mTerrainUploadMs+=elapsed();return g.ready;
}
glm::vec3 D3DRenderAdapter::TerrainCameraPosition() const {return {mMainPassCB.EyePosW.x,mMainPassCB.EyePosW.y,mMainPassCB.EyePosW.z};}
bool D3DRenderAdapter::TerrainVisible(const glm::vec3& lo,const glm::vec3& hi) const
{
    const auto& m=mMainPassCB.ViewProj.m;
    for(int plane=0;plane<6;++plane)
    {
        glm::vec4 p;
        for(int i=0;i<4;++i)p[i]=plane==4?m[2][i] : m[3][i]+(plane==0||plane==2?1.f:-1.f)*m[plane<2?0:plane<4?1:2][i];
        const glm::vec3 v(p.x>=0?hi.x:lo.x,p.y>=0?hi.y:lo.y,p.z>=0?hi.z:lo.z);
        if(glm::dot(glm::vec3(p),v)+p.w<0)return false;
    }
    return true;
}
void D3DRenderAdapter::DrawTerrain(const TerrainResource& r,const TransformComponent& t,const TerrainComponent& options,const std::vector<TerrainDrawChunk>& visible)
{
    const auto found=mTerrainGPU.find(&r);if(found==mTerrainGPU.end()||!found->second->ready)return;
    const auto& g=*found->second;const auto& s=r.settings;
    ID3D12DescriptorHeap* heaps[]={mCbvSrvUavHeap.Get()};mCommandList->SetDescriptorHeaps(1,heaps);
    mCommandList->SetGraphicsRootSignature(mRootSignatures["terrain"].Get());
    mCommandList->SetGraphicsRootDescriptorTable(0,GetGPUDescriptorHandle(g.descriptors));
    mCommandList->SetGraphicsRootConstantBufferView(1,mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRoot32BitConstant(3, mRenderEntityID, 0);
    struct Constants {glm::vec4 origin,scale,map,chunk,color[4],tiles,debug,lo,hi;} data;
    static_assert(sizeof(Constants)==48*4);
    data.origin={t.position,0};data.scale={s.sizeX/(s.samplesX-1)*t.scale.x,t.scale.y,s.sizeZ/(s.samplesZ-1)*t.scale.z,0};
    for(int i=0;i<4;++i){data.color[i]={s.layers[i].color,s.layers[i].roughness};data.tiles[i]=s.layers[i].tileSize;}
    uint32_t normalFlags=0; // low four bits: XY normals; high four: signed XY data
    for(int i=0;i<4;++i)
    {
        const auto f=r.images[i*2+1].format;
        if(f==DXGI_FORMAT_BC5_UNORM||f==DXGI_FORMAT_BC5_SNORM||f==DXGI_FORMAT_R8G8_UNORM||f==DXGI_FORMAT_R8G8_SNORM)normalFlags|=1u<<i;
        if(f==DXGI_FORMAT_BC5_SNORM||f==DXGI_FORMAT_R8G8_SNORM)normalFlags|=1u<<(i+4);
    }
    for(auto draw:visible)
    {
        const auto& c=r.chunks[draw.chunk];data.map={float(s.samplesX),float(s.samplesZ),float(c.x),float(c.z)};
        data.chunk={float(c.cellsX),float(c.cellsZ),c.skirt,float(s.layerCount)};data.debug={options.showLod?1.f:0.f,float(draw.lod),float(normalFlags),0};
        data.lo={t.position+c.min*t.scale,0};data.hi={t.position+c.max*t.scale,0};
        mCommandList->SetPipelineState(mPSOs[mEditorScene ? (options.wireframe ? "terrainWireEditor" : "terrainEditor") : (options.wireframe ? "terrainWire" : "terrain")].Get());
        mCommandList->SetGraphicsRoot32BitConstants(2,48,&data,0);
        D3D12_VERTEX_BUFFER_VIEW vb{g.vertices[draw.lod]->GetGPUVirtualAddress(),g.vertexBytes[draw.lod],sizeof(glm::vec3)};
        D3D12_INDEX_BUFFER_VIEW ib{g.indices[draw.lod]->GetGPUVirtualAddress(),g.indexCounts[draw.lod]*UINT(sizeof(uint32_t)),DXGI_FORMAT_R32_UINT};
        mCommandList->IASetVertexBuffers(0,1,&vb);mCommandList->IASetIndexBuffer(&ib);mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mCommandList->DrawIndexedInstanced(g.indexCounts[draw.lod],1,0,0,0);
        if(options.showBounds){mCommandList->SetPipelineState(mPSOs[mEditorScene ? "terrainBoundsEditor" : "terrainBounds"].Get());mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);mCommandList->DrawInstanced(24,1,0,0);}
    }
    mCommandList->SetPipelineState(mPSOs[mEditorScene ? "opaqueEditor" : "opaque"].Get());mCommandList->SetGraphicsRootSignature(mRootSignatures["standard"].Get());
    mCommandList->SetGraphicsRootConstantBufferView(3,mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
}
