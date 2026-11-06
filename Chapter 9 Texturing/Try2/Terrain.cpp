#include "Terrain.h"
#include "ResourceManager.h"
#include "AssetPaths.h"
#include "ThirdParty/stb/stb_image.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>
#include <limits>

using nlohmann::json;
namespace
{
    void Check(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
    bool Finite(float x) { return std::isfinite(x); }
    float Noise(int x, int z, uint32_t seed)
    {
        uint32_t n = uint32_t(x)*374761393u + uint32_t(z)*668265263u + seed*1442695041u;
        n = (n ^ (n >> 13))*1274126177u; n ^= n >> 16;
        return float(n & 0xffffff) / float(0xffffff);
    }
    float SmoothNoise(float x, float z, uint32_t seed)
    {
        const int ix = int(std::floor(x)), iz = int(std::floor(z));
        x -= ix; z -= iz; x=x*x*(3-2*x); z=z*z*(3-2*z);
        return glm::mix(glm::mix(Noise(ix,iz,seed),Noise(ix+1,iz,seed),x),
            glm::mix(Noise(ix,iz+1,seed),Noise(ix+1,iz+1,seed),x),z);
    }
    void Cancel(const std::atomic<bool>& cancelled) { if (cancelled) throw std::runtime_error("Terrain build cancelled"); }
    std::vector<float> ReadHeight(const TerrainComponent& s, const std::filesystem::path& root)
    {
        const auto path = AssetPaths::Resolve(root,s.heightmap);
        std::vector<float> data(size_t(s.samplesX)*s.samplesZ);
        auto ext = path.extension().wstring();
        std::transform(ext.begin(),ext.end(),ext.begin(),towlower);
        if (ext == L".dds")
        {
            auto image = ResourceManager::DecodeImage(path);
            Check(image.width==s.samplesX && image.height==s.samplesZ && image.arraySize==1 && image.depth==1 && !image.isCubeMap,
                "Heightmap dimensions must equal configured samples X/Z; only one 2D image is supported");
            const auto& sub = image.subresources.at(0);
            for (uint32_t z=0;z<s.samplesZ;++z) for (uint32_t x=0;x<s.samplesX;++x)
            {
                const auto* p=image.pixels.data()+sub.offset+z*sub.rowPitch;
                float h;
                if (image.format==DXGI_FORMAT_R16_UNORM) { uint16_t n; memcpy(&n,p+x*2,2); h=n/65535.f; }
                else if (image.format==DXGI_FORMAT_R8_UNORM) h=p[x]/255.f;
                else if (image.format==DXGI_FORMAT_R32_FLOAT) memcpy(&h,p+x*4,4);
                else throw std::runtime_error("DDS heightmap requires linear R8_UNORM, R16_UNORM or R32_FLOAT (no compressed/color formats)");
                Check(Finite(h) && h>=0 && h<=1,"Height samples must be finite and within [0,1]");
                data[size_t(z)*s.samplesX+x]=h;
            }
        }
        else
        {
            FILE* handle=nullptr; _wfopen_s(&handle,path.c_str(),L"rb");
            Check(handle!=nullptr,"Cannot open heightmap file");
            std::unique_ptr<FILE,decltype(&fclose)> file(handle,&fclose);
            int w=0,h=0,c=0;
            Check(stbi_info_from_file(file.get(),&w,&h,&c)!=0,"Cannot decode heightmap header");
            Check(w==s.samplesX && h==s.samplesZ && c==1,"Heightmap must be single-channel and match configured samples X/Z");
            rewind(file.get());
            Check(!stbi_is_hdr_from_file(file.get()),"HDR heightmap is unsupported; use 16-bit grayscale PNG or R32_FLOAT DDS");
            rewind(file.get());
            const bool high = stbi_is_16_bit_from_file(file.get())!=0; rewind(file.get());
            Check(!high||ext==L".png","16-bit height images must be grayscale PNG or R16_UNORM DDS; other 16-bit formats are unsupported");
            if (high)
            {
                std::unique_ptr<stbi_us,decltype(&stbi_image_free)> p(stbi_load_from_file_16(file.get(),&w,&h,&c,1),&stbi_image_free);
                Check(bool(p),"Failed to decode 16-bit heightmap");
                for (size_t i=0;i<data.size();++i) data[i]=p.get()[i]/65535.f;
            }
            else
            {
                std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> p(stbi_load_from_file(file.get(),&w,&h,&c,1),&stbi_image_free);
                Check(bool(p),"Failed to decode 8-bit heightmap");
                for (size_t i=0;i<data.size();++i) data[i]=p.get()[i]/255.f;
            }
        }
        return data;
    }
    TerrainGrid Grid(uint32_t cells, uint32_t step)
    {
        TerrainGrid g; const uint32_t n=cells/step;
        for (uint32_t z=0;z<=n;++z) for (uint32_t x=0;x<=n;++x) g.vertices.emplace_back(float(x*step),0,float(z*step));
        const auto quad=[&](uint32_t a,uint32_t b,uint32_t c,uint32_t d) { g.indices.insert(g.indices.end(),{a,c,b,b,c,d}); };
        for (uint32_t z=0;z<n;++z) for (uint32_t x=0;x<n;++x) { auto a=z*(n+1)+x; quad(a,a+1,a+n+1,a+n+2); }
        std::vector<uint32_t> edge;
        for (uint32_t x=0;x<=n;++x) edge.push_back(x);
        for (uint32_t z=1;z<=n;++z) edge.push_back(z*(n+1)+n);
        for (uint32_t x=n;x>0;--x) edge.push_back(n*(n+1)+x-1);
        for (uint32_t z=n;z>1;--z) edge.push_back((z-1)*(n+1));
        const uint32_t start=uint32_t(g.vertices.size());
        for (auto i:edge) { auto p=g.vertices[i]; p.y=1; g.vertices.push_back(p); }
        for (uint32_t i=0;i<edge.size();++i) { auto j=(i+1)%uint32_t(edge.size()); quad(edge[i],edge[j],start+i,start+j); }
        return g;
    }
}
void Terrain::Validate(const TerrainComponent& s)
{
    Check(s.source=="flat"||s.source=="procedural"||s.source=="heightmap","Unknown terrain source");
    Check(s.samplesX>=3 && s.samplesZ>=3 && s.samplesX<=8193 && s.samplesZ<=8193,"Terrain samples must be between 3 and 8193");
    Check(size_t(s.samplesX)*s.samplesZ<=17*1024*1024,"Terrain sample count exceeds the 17M safety limit");
    Check(s.chunkCells>=8 && s.chunkCells<=256 && (s.chunkCells&(s.chunkCells-1))==0,"Chunk cells must be a power of two between 8 and 256");
    Check(s.lodCount>=1 && s.lodCount<=8 && (1u<<(s.lodCount-1))<=s.chunkCells,"LOD count exceeds chunk resolution");
    Check(Finite(s.sizeX)&&Finite(s.sizeZ)&&s.sizeX>0 && s.sizeZ>0 && s.sizeX<=1e6f && s.sizeZ<=1e6f,"Invalid terrain dimensions");
    Check(Finite(s.heightScale)&&Finite(s.heightOffset)&&s.heightScale>=0&&s.heightScale<=1e5f&&std::abs(s.heightOffset)<=1e6f,"Invalid terrain height range");
    Check(Finite(s.frequency)&&s.frequency>0&&s.frequency<=1 && Finite(s.lodDistance)&&s.lodDistance>0,"Invalid terrain frequency/LOD distance");
    Check(Finite(s.hysteresis)&&s.hysteresis>=0&&s.hysteresis<.5f&&Finite(s.skirtDepth)&&s.skirtDepth>0&&s.skirtDepth<1e5f,"Invalid hysteresis/skirt depth");
    Check(s.layerCount>=1 && s.layerCount<=4,"Terrain supports 1..4 layers");
    Check(Finite(s.friction)&&s.friction>=0&&s.friction<=1&&Finite(s.restitution)&&s.restitution>=0&&s.restitution<=1,"Invalid terrain friction/restitution");
    Check(size_t((s.samplesX-2)/s.chunkCells+1)*((s.samplesZ-2)/s.chunkCells+1)<=16384,"Terrain exceeds 16384 chunks");
    for (auto& l:s.layers) Check(Finite(l.tileSize)&&l.tileSize>0 && Finite(l.roughness)&&l.roughness>=0&&l.roughness<=1 &&
        Finite(l.color.x)&&Finite(l.color.y)&&Finite(l.color.z),"Invalid terrain layer parameters");
}
bool Terrain::ValidTransform(const TransformComponent& t)
{
    for (int i=0;i<3;++i) if (!Finite(t.position[i])||!Finite(t.scale[i])||t.scale[i]<=0||t.scale[i]>1e4f||!Finite(t.rotation[i])||std::abs(t.rotation[i])>1e-6f) return false;
    return true;
}
size_t Terrain::EstimateBytes(const TerrainComponent& s) { Validate(s); return size_t(s.samplesX)*s.samplesZ*8+8*1024*1024; }
size_t TerrainResource::Bytes() const
{
    size_t n=heights.size()*4+weights.size()+chunks.size()*sizeof(TerrainChunk);
    for(auto& g:grids) n+=g.vertices.size()*sizeof(glm::vec3)+g.indices.size()*sizeof(uint32_t);
    for(auto& i:images) n+=i.pixels.size(); return n;
}
float TerrainResource::Height(uint32_t x,uint32_t z) const { return heights[size_t(z)*settings.samplesX+x]; }
glm::vec3 TerrainResource::Point(uint32_t x,uint32_t z) const { return {x*settings.sizeX/(settings.samplesX-1),Height(x,z),z*settings.sizeZ/(settings.samplesZ-1)}; }
bool TerrainResource::Cell(float x,float z,uint32_t& ix,uint32_t& iz,float& u,float& v) const
{
    if(!Finite(x)||!Finite(z)||x<0||z<0||x>settings.sizeX||z>settings.sizeZ) return false;
    x=x/settings.sizeX*(settings.samplesX-1); z=z/settings.sizeZ*(settings.samplesZ-1);
    ix=std::min(uint32_t(x),settings.samplesX-2); iz=std::min(uint32_t(z),settings.samplesZ-2); u=x-ix;v=z-iz;return true;
}
bool TerrainResource::TryGetHeight(const TransformComponent& t,float x,float z,float& height) const
{
    if(!Terrain::ValidTransform(t)) return false;
    uint32_t ix,iz; float u,v;
    if(!Cell((x-t.position.x)/t.scale.x,(z-t.position.z)/t.scale.z,ix,iz,u,v)) return false;
    const float a=Height(ix,iz),b=Height(ix+1,iz),c=Height(ix,iz+1),d=Height(ix+1,iz+1);
    height=t.position.y+t.scale.y*(u+v<=1 ? a+(b-a)*u+(c-a)*v : d+(c-d)*(1-u)+(b-d)*(1-v)); return true;
}
bool TerrainResource::TryGetNormal(const TransformComponent& t,float x,float z,glm::vec3& n) const
{
    if(!Terrain::ValidTransform(t)) return false;
    uint32_t ix,iz;float u,v;if(!Cell((x-t.position.x)/t.scale.x,(z-t.position.z)/t.scale.z,ix,iz,u,v)) return false;
    const auto a=Point(ix,iz)*t.scale,b=Point(ix+1,iz)*t.scale,c=Point(ix,iz+1)*t.scale,d=Point(ix+1,iz+1)*t.scale;
    n=glm::normalize(u+v<=1 ? glm::cross(c-a,b-a) : glm::cross(c-b,d-b));return true;
}
std::shared_ptr<TerrainResource> Terrain::Build(const TerrainComponent& s,const std::filesystem::path& root,const std::atomic<bool>& cancelled)
{
    ZoneScopedN("Terrain CPU build"); Validate(s); const auto start=std::chrono::steady_clock::now();
    auto r=std::make_shared<TerrainResource>();r->settings=s;
    const size_t count=size_t(s.samplesX)*s.samplesZ;
    r->heights = s.source=="heightmap" ? ReadHeight(s,root) : std::vector<float>(count);
    for(uint32_t z=0;z<s.samplesZ;++z)
    {
        Cancel(cancelled);
        for(uint32_t x=0;x<s.samplesX;++x)
        {
            float h=r->heights[size_t(z)*s.samplesX+x];
            if(s.source=="procedural")
            {
                const float px=x*s.sizeX/(s.samplesX-1)*s.frequency,pz=z*s.sizeZ/(s.samplesZ-1)*s.frequency;
                h=.57f*SmoothNoise(px,pz,s.seed)+.28f*SmoothNoise(px*2,pz*2,s.seed+1)+.15f*SmoothNoise(px*4,pz*4,s.seed+2);
            }
            r->heights[size_t(z)*s.samplesX+x]=s.heightOffset+h*s.heightScale;
        }
    }
    r->weights.resize(count*4);
    if(!s.weightmap.empty())
    {
        auto image=ResourceManager::DecodeImage(AssetPaths::Resolve(root,s.weightmap));
        Check(image.width==s.samplesX&&image.height==s.samplesZ&&image.format==DXGI_FORMAT_R8G8B8A8_UNORM&&image.arraySize==1,
            "Weight map must be linear RGBA8 and match the heightfield samples");
        for(uint32_t z=0;z<s.samplesZ;++z) memcpy(r->weights.data()+size_t(z)*s.samplesX*4,image.pixels.data()+image.subresources[0].offset+z*image.subresources[0].rowPitch,size_t(s.samplesX)*4);
    }
    for(uint32_t z=0;z<s.samplesZ;++z)
    {
        Cancel(cancelled);
        for(uint32_t x=0;x<s.samplesX;++x)
        {
            const size_t i=(size_t(z)*s.samplesX+x)*4;
            glm::vec4 w;
            if(!s.weightmap.empty()) for(int j=0;j<4;++j) w[j]=r->weights[i+j];
            else
            {
                const float h=(r->Height(x,z)-s.heightOffset)/std::max(1.f,s.heightScale);
                const float dx=(r->Height(std::min(x+1,s.samplesX-1),z)-r->Height(x?x-1:0,z))/(2*s.sizeX/(s.samplesX-1));
                const float dz=(r->Height(x,std::min(z+1,s.samplesZ-1))-r->Height(x,z?z-1:0))/(2*s.sizeZ/(s.samplesZ-1));
                const float rock=glm::clamp(std::sqrt(dx*dx+dz*dz)*1.5f,0.f,1.f);
                w={std::max(0.f,1-h*1.6f)*(1-rock),.3f*(1-rock),rock,std::max(0.f,(h-.65f)*4)};
            }
            for(uint32_t j=s.layerCount;j<4;++j) w[j]=0;
            const float sum=w.x+w.y+w.z+w.w;w=sum>0?w/sum:glm::vec4(1,0,0,0);
            for(int j=0;j<4;++j) r->weights[i+j]=uint8_t(glm::clamp(w[j]*255.f,0.f,255.f));
        }
    }
    for(uint32_t z=0;z<s.samplesZ-1;z+=s.chunkCells) for(uint32_t x=0;x<s.samplesX-1;x+=s.chunkCells)
    {
        Cancel(cancelled); TerrainChunk c; c.x=x;c.z=z;c.cellsX=std::min(s.chunkCells,s.samplesX-1-x);c.cellsZ=std::min(s.chunkCells,s.samplesZ-1-z);
        float lo=std::numeric_limits<float>::max(),hi=-lo;
        for(uint32_t j=z;j<=z+c.cellsZ;++j) for(uint32_t i=x;i<=x+c.cellsX;++i) {auto h=r->Height(i,j);lo=std::min(lo,h);hi=std::max(hi,h);}
        c.skirt=std::max(s.skirtDepth,hi-lo+.01f);c.min=r->Point(x,z);c.max=r->Point(x+c.cellsX,z+c.cellsZ);c.min.y=lo-c.skirt;c.max.y=hi;
        r->chunks.push_back(c);
    }
    for(uint32_t lod=0;lod<s.lodCount;++lod) r->grids.push_back(Grid(s.chunkCells,1u<<lod));
    for(uint32_t layer=0;layer<s.layerCount;++layer) for(uint32_t type=0;type<2;++type)
    {
        Cancel(cancelled);const auto& file=type?s.layers[layer].normal:s.layers[layer].albedo;
        if(!file.empty())
        {
            const auto path=AssetPaths::Resolve(root,file);
            Check(std::filesystem::file_size(path)<=32*1024*1024,"Terrain layer file exceeds 32 MiB");
            auto ext=path.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);
            if(ext!=L".dds")
            {
                FILE* raw=nullptr;_wfopen_s(&raw,path.c_str(),L"rb");Check(raw!=nullptr,"Cannot open terrain layer");
                std::unique_ptr<FILE,decltype(&fclose)> input(raw,&fclose);int w,h,channels;
                Check(stbi_info_from_file(input.get(),&w,&h,&channels)!=0&&w>0&&h>0&&uint64_t(w)*h<=8*1024*1024,
                    "Terrain layer exceeds 32 MiB decoded RGBA budget or has an invalid header");
            }
            auto image=ResourceManager::DecodeImage(path);
            Check(image.dimension==ImageDimension::Texture2D&&image.arraySize==1&&!image.isCubeMap&&image.subresources.at(0).slicePitch<=32*1024*1024,"Terrain layer must be one 2D texture, at most 32 MiB decoded (top mip)");
            if(type==1) // Normal vectors are linear data even if the DDS was tagged sRGB.
            {
                switch(image.format)
                {
                case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:image.format=DXGI_FORMAT_R8G8B8A8_UNORM;break;
                case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:image.format=DXGI_FORMAT_B8G8R8A8_UNORM;break;
                case DXGI_FORMAT_BC1_UNORM_SRGB:image.format=DXGI_FORMAT_BC1_UNORM;break;
                case DXGI_FORMAT_BC2_UNORM_SRGB:image.format=DXGI_FORMAT_BC2_UNORM;break;
                case DXGI_FORMAT_BC3_UNORM_SRGB:image.format=DXGI_FORMAT_BC3_UNORM;break;
                case DXGI_FORMAT_BC7_UNORM_SRGB:image.format=DXGI_FORMAT_BC7_UNORM;break;
                default:break;
                }
            }
            r->images[layer*2+type]=std::move(image);
        }
        Check(r->Bytes()<=192*1024*1024,"Terrain CPU result exceeds 192 MiB memory budget");
    }
    r->buildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); return r;
}
uint32_t Terrain::SelectLod(float distance,uint32_t previous,const TerrainComponent& s)
{
    uint32_t lod=std::min(previous,s.lodCount-1);
    while(lod+1<s.lodCount && distance>s.lodDistance*float(1u<<lod)*(1+s.hysteresis)) ++lod;
    while(lod>0 && distance<s.lodDistance*float(1u<<(lod-1))*(1-s.hysteresis)) --lod;
    return lod;
}
json Terrain::ToJson(const TerrainComponent& s)
{
    json j={{"source",s.source},{"heightmap",s.heightmap},{"weightmap",s.weightmap},{"samplesX",s.samplesX},{"samplesZ",s.samplesZ},
        {"sizeX",s.sizeX},{"sizeZ",s.sizeZ},{"heightScale",s.heightScale},{"heightOffset",s.heightOffset},{"chunkCells",s.chunkCells},
        {"seed",s.seed},{"frequency",s.frequency},{"lodCount",s.lodCount},{"lodDistance",s.lodDistance},{"hysteresis",s.hysteresis},{"skirtDepth",s.skirtDepth},
        {"layerCount",s.layerCount},{"culling",s.culling},{"wireframe",s.wireframe},{"showLod",s.showLod},{"showBounds",s.showBounds},
        {"collision",s.collision},{"friction",s.friction},{"restitution",s.restitution}};
    j["layers"]=json::array();for(auto& l:s.layers) j["layers"].push_back({{"albedo",l.albedo},{"normal",l.normal},{"color",{l.color.x,l.color.y,l.color.z}},{"roughness",l.roughness},{"tileSize",l.tileSize}});
    return j;
}
TerrainComponent Terrain::FromJson(const json& j)
{
    TerrainComponent s;
#define FIELD(f) s.f=j.value(#f,s.f)
    FIELD(source);FIELD(heightmap);FIELD(weightmap);FIELD(samplesX);FIELD(samplesZ);FIELD(sizeX);FIELD(sizeZ);FIELD(heightScale);FIELD(heightOffset);FIELD(chunkCells);
    FIELD(seed);FIELD(frequency);FIELD(lodCount);FIELD(lodDistance);FIELD(hysteresis);FIELD(skirtDepth);FIELD(layerCount);FIELD(culling);FIELD(wireframe);FIELD(showLod);FIELD(showBounds);FIELD(collision);FIELD(friction);FIELD(restitution);
#undef FIELD
    if(j.contains("layers"))
    {
        Check(j["layers"].is_array()&&j["layers"].size()<=4,"Expected at most four terrain layers");
        size_t i=0;for(const auto& a:j["layers"])
        {
            auto& l=s.layers[i++];l.albedo=a.value("albedo",l.albedo);l.normal=a.value("normal",l.normal);l.tileSize=a.value("tileSize",l.tileSize);l.roughness=a.value("roughness",l.roughness);
            if(a.contains("color")) {const auto& c=a.at("color");Check(c.is_array()&&c.size()==3,"Terrain layer color needs 3 values");l.color={c[0].get<float>(),c[1].get<float>(),c[2].get<float>()};}
        }
    }
    Validate(s);return s;
}

namespace
{
    // Ericson's closest point on triangle; no triangle copies are stored per cell.
    glm::vec3 Closest(glm::vec3 p,glm::vec3 a,glm::vec3 b,glm::vec3 c)
    {
        auto ab=b-a,ac=c-a,ap=p-a;float d1=dot(ab,ap),d2=dot(ac,ap);if(d1<=0&&d2<=0)return a;
        auto bp=p-b;float d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return b;
        float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return a+ab*(d1/(d1-d3));
        auto cp=p-c;float d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return c;
        float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return a+ac*(d2/(d2-d6));
        float va=d3*d6-d5*d4;if(va<=0&&d4-d3>=0&&d5-d6>=0)return b+(c-b)*((d4-d3)/(d4-d3+d5-d6));
        return a+ab*(vb/(va+vb+vc))+ac*(vc/(va+vb+vc));
    }
    float BoxHeight(glm::vec3 a,glm::vec3 b,glm::vec3 c,const AABB& box)
    {
        // Clip the triangle to the whole XZ footprint; includes interior peaks and
        // intersections on cell/box edges, unlike centre/corner-only sampling.
        std::vector<glm::vec3> p={a,b,c},out;const auto lo=box.Min(),hi=box.Max();
        for(int plane=0;plane<4&&!p.empty();++plane)
        {
            int axis=plane/2==0?0:2;bool low=(plane%2)==0;float edge=low?lo[axis]:hi[axis];out.clear();
            auto inside=[&](glm::vec3 v){return low?v[axis]>=edge:v[axis]<=edge;};
            auto prev=p.back();for(auto v:p)
            {
                if(inside(prev)!=inside(v)) out.push_back(prev+(v-prev)*((edge-prev[axis])/(v[axis]-prev[axis])));
                if(inside(v)) out.push_back(v);prev=v;
            }p.swap(out);
        }
        float h=-std::numeric_limits<float>::max();for(auto v:p)h=std::max(h,v.y);return h;
    }
}
bool Terrain::Contact(const TerrainResource& r,const TransformComponent& t,const AABB& body,ColliderType type,glm::vec3& normal,float& penetration)
{
    if(!ValidTransform(t))return false;
    const auto& s=r.settings; const auto lo=(body.Min()-t.position)/t.scale,hi=(body.Max()-t.position)/t.scale;
    if(hi.x<0||hi.z<0||lo.x>s.sizeX||lo.z>s.sizeZ) return false;
    const uint32_t x0=uint32_t(glm::clamp(lo.x/s.sizeX*(s.samplesX-1),0.f,float(s.samplesX-2))),
        z0=uint32_t(glm::clamp(lo.z/s.sizeZ*(s.samplesZ-1),0.f,float(s.samplesZ-2))),
        x1=uint32_t(glm::clamp(hi.x/s.sizeX*(s.samplesX-1),0.f,float(s.samplesX-2))),
        z1=uint32_t(glm::clamp(hi.z/s.sizeZ*(s.samplesZ-1),0.f,float(s.samplesZ-2)));
    penetration=0;normal={0,1,0};
    for(uint32_t z=z0;z<=z1;++z)for(uint32_t x=x0;x<=x1;++x)
    {
        auto a=t.position+r.Point(x,z)*t.scale,b=t.position+r.Point(x+1,z)*t.scale,c=t.position+r.Point(x,z+1)*t.scale,d=t.position+r.Point(x+1,z+1)*t.scale;
        for(int i=0;i<2;++i)
        {
            const auto p0=i?b:a,p1=c,p2=i?d:b;auto n=normalize(cross(p1-p0,p2-p0));float depth=0;
            if(type==ColliderType::Sphere)
            {
                auto q=Closest(body.center,p0,p1,p2),delta=body.center-q;float distance=length(delta),signedDist=dot(delta,n);
                if(distance<body.halfExtents.x) {depth=body.halfExtents.x+(signedDist<0?distance:-distance);if(distance>1e-6f&&signedDist>=0)n=delta/distance;}
                // Solid below surface: recover bodies which start slightly buried.
                else if(signedDist<0 && std::abs(delta.x)<1e-4f&&std::abs(delta.z)<1e-4f) depth=body.halfExtents.x-signedDist;
            }
            else {float h=BoxHeight(p0,p1,p2,body);if(h>body.Min().y)depth=(h-body.Min().y)*n.y;}
            if(depth>penetration){penetration=depth;normal=n;}
        }
    }
    return penetration>0;
}
