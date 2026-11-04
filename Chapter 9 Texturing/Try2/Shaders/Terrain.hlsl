#define NUM_DIR_LIGHTS 1
#define NUM_POINT_LIGHTS 2
#define NUM_SPOT_LIGHTS 3
#include "LightingUtil.hlsl"
Texture2D<float> Heights : register(t0);
Texture2D<float4> Weights : register(t1);
Texture2D Layers[8] : register(t2);
SamplerState LinearWrap : register(s2);
SamplerState LinearClamp : register(s3);
SamplerState AnisoWrap : register(s4);
cbuffer TerrainDraw : register(b0)
{
    float4 Origin, Scale, Map, Chunk;
    float4 LayerColor[4];
    float4 Tiles, Debug, BoundsMin, BoundsMax;
};
cbuffer cbPass : register(b1)
{
    float4x4 gView,gInvView,gProj,gInvProj,gViewProj,gInvViewProj;
    float3 gEyePosW;float Padding;
    float2 gRenderTargetSize,gInvRenderTargetSize;
    float gNearZ,gFarZ,gTotalTime,gDeltaTime;
    float4 gAmbientLight;
    Light gLights[MaxLights];
};
struct Out {float4 pos:SV_POSITION;float3 world:POSITION;float2 sampleCoord:TEXCOORD;float3 normal:NORMAL;float3 tangent:TANGENT;};
float H(int2 p) {return Heights.Load(int3(clamp(p,int2(0,0),int2(Map.xy)-1),0));}
Out VSTerrain(float3 grid:POSITION)
{
    Out o;float2 sampleCoord=Map.zw+min(grid.xz,Chunk.xy);int2 p=int2(sampleCoord);
    float h=H(p);float hx=(H(p+int2(1,0))-H(p-int2(1,0)))*Scale.y;
    float hz=(H(p+int2(0,1))-H(p-int2(0,1)))*Scale.y;
    float dx=(min(p.x+1,Map.x-1)-max(p.x-1,0))*Scale.x;
    float dz=(min(p.y+1,Map.y-1)-max(p.y-1,0))*Scale.z;
    o.normal=normalize(float3(-hx/dx,1,-hz/dz));o.tangent=normalize(float3(1,hx/dx,0));
    o.world=Origin.xyz+float3(sampleCoord.x*Scale.x,(h-grid.y*Chunk.z)*Scale.y,sampleCoord.y*Scale.z);
    o.sampleCoord=sampleCoord;o.pos=mul(float4(o.world,1),gViewProj);return o;
}
float4 PSTerrain(Out i):SV_Target
{
    float4 w=Weights.Sample(LinearClamp,(i.sampleCoord+.5)/Map.xy);
    [unroll]for(int j=0;j<4;++j)if(j>=Chunk.w)w[j]=0;
    float sum=dot(w,1);w=sum>0?w/sum:float4(1,0,0,0);
    float3 albedo=0,normalT=0;float rough=0;
    [unroll]for(int l=0;l<4;++l)
    {
        float2 uv=(i.world.xz-Origin.xz)/Tiles[l];
        albedo+=w[l]*Layers[l*2].Sample(AnisoWrap,uv).rgb*LayerColor[l].rgb;
        float3 sampled=Layers[l*2+1].Sample(AnisoWrap,uv).xyz;
        float3 layerNormal=sampled*2-1;
        if(((uint)Debug.z & (1u<<l))!=0)
        {
            float2 xy=((uint)Debug.z & (1u<<(l+4)))!=0?sampled.xy:sampled.xy*2-1;
            layerNormal=float3(xy,sqrt(saturate(1-dot(xy,xy))));
        }
        normalT+=w[l]*layerNormal;
        rough+=w[l]*LayerColor[l].a;
    }
    float3 N=normalize(i.normal),T=normalize(i.tangent-dot(i.tangent,N)*N),B=cross(T,N);
    normalT=dot(normalT,normalT)>1e-8?normalize(normalT):float3(0,0,1);
    float3 n=normalize(mul(normalT,float3x3(T,B,N)));
    if(Debug.x>0)
    {
        const float3 colors[8]={float3(.2,.8,.2),float3(.8,.8,.1),float3(.8,.3,.1),float3(.2,.5,.9),float3(.8,.2,.8),float3(.2,.8,.8),float3(.9,.9,.9),float3(.5,.5,.5)};
        albedo=colors[(int)Debug.y];
    }
    Material mat={float4(albedo,1),float3(.04,.04,.04),1-rough};
    float4 light=ComputeLighting(gLights,mat,i.world,n,normalize(gEyePosW-i.world),1);
    return float4(gAmbientLight.rgb*albedo+light.rgb,1);
}
float4 VSBounds(uint id:SV_VertexID):SV_POSITION
{
    const uint edges[24]={0,1,1,3,3,2,2,0,4,5,5,7,7,6,6,4,0,4,1,5,2,6,3,7};
    uint c=edges[id];float3 p=float3(c&1?BoundsMax.x:BoundsMin.x,c&2?BoundsMax.y:BoundsMin.y,c&4?BoundsMax.z:BoundsMin.z);
    return mul(float4(p,1),gViewProj);
}
float4 PSBounds():SV_Target {return float4(1,.7,.05,1);}
