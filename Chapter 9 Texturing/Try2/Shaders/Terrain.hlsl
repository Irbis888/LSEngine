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
    float4 Origin;
    float4 Scale;
    float4 Map;
    float4 Chunk;
    float4 LayerColor[4];
    float4 Tiles;
    float4 Debug;
    float4 BoundsMin;
    float4 BoundsMax;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float Padding;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    Light gLights[MaxLights];
};

struct TerrainVertexOutput
{
    float4 clipPosition : SV_POSITION;
    float3 worldPosition : POSITION;
    float2 sampleCoordinates : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};

float LoadHeight(int2 samplePosition)
{
    int2 clampedPosition = clamp(samplePosition, int2(0, 0), int2(Map.xy) - 1);
    return Heights.Load(int3(clampedPosition, 0));
}

TerrainVertexOutput VSTerrain(float3 gridPosition : POSITION)
{
    TerrainVertexOutput output;
    float2 sampleCoordinates = Map.zw + min(gridPosition.xz, Chunk.xy);
    int2 samplePosition = int2(sampleCoordinates);
    float height = LoadHeight(samplePosition);
    float heightDifferenceX = (LoadHeight(samplePosition + int2(1, 0)) - LoadHeight(samplePosition - int2(1, 0))) * Scale.y;
    float heightDifferenceZ = (LoadHeight(samplePosition + int2(0, 1)) - LoadHeight(samplePosition - int2(0, 1))) * Scale.y;
    float sampleDistanceX = (min(samplePosition.x + 1, Map.x - 1) - max(samplePosition.x - 1, 0)) * Scale.x;
    float sampleDistanceZ = (min(samplePosition.y + 1, Map.y - 1) - max(samplePosition.y - 1, 0)) * Scale.z;

    output.normal = normalize(float3(-heightDifferenceX / sampleDistanceX, 1, -heightDifferenceZ / sampleDistanceZ));
    output.tangent = normalize(float3(1, heightDifferenceX / sampleDistanceX, 0));
    output.worldPosition = Origin.xyz + float3(
        sampleCoordinates.x * Scale.x,
        (height - gridPosition.y * Chunk.z) * Scale.y,
        sampleCoordinates.y * Scale.z);
    output.sampleCoordinates = sampleCoordinates;
    output.clipPosition = mul(float4(output.worldPosition, 1), gViewProj);
    return output;
}

float4 ShadeSurface(TerrainVertexOutput input)
{
    float4 layerWeights = Weights.Sample(LinearClamp, (input.sampleCoordinates + 0.5) / Map.xy);
    [unroll]
    for (int layerIndex = 0; layerIndex < 4; ++layerIndex)
    {
        if (layerIndex >= Chunk.w)
        {
            layerWeights[layerIndex] = 0;
        }
    }

    float weightSum = dot(layerWeights, 1);
    layerWeights = weightSum > 0 ? layerWeights / weightSum : float4(1, 0, 0, 0);
    float3 albedo = 0;
    float3 tangentSpaceNormal = 0;
    float roughness = 0;

    [unroll]
    for (int layerIndex = 0; layerIndex < 4; ++layerIndex)
    {
        float2 textureCoordinates = (input.worldPosition.xz - Origin.xz) / Tiles[layerIndex];
        albedo += layerWeights[layerIndex] * Layers[layerIndex * 2].Sample(AnisoWrap, textureCoordinates).rgb * LayerColor[layerIndex].rgb;
        float3 normalSample = Layers[layerIndex * 2 + 1].Sample(AnisoWrap, textureCoordinates).xyz;
        float3 layerNormal = normalSample * 2 - 1;
        if (((uint)Debug.z & (1u << layerIndex)) != 0)
        {
            float2 normalXY = ((uint)Debug.z & (1u << (layerIndex + 4))) != 0
                ? normalSample.xy
                : normalSample.xy * 2 - 1;
            layerNormal = float3(normalXY, sqrt(saturate(1 - dot(normalXY, normalXY))));
        }
        tangentSpaceNormal += layerWeights[layerIndex] * layerNormal;
        roughness += layerWeights[layerIndex] * LayerColor[layerIndex].a;
    }

    float3 surfaceNormal = normalize(input.normal);
    float3 surfaceTangent = normalize(input.tangent - dot(input.tangent, surfaceNormal) * surfaceNormal);
    float3 surfaceBitangent = cross(surfaceTangent, surfaceNormal);
    tangentSpaceNormal = dot(tangentSpaceNormal, tangentSpaceNormal) > 1e-8
        ? normalize(tangentSpaceNormal)
        : float3(0, 0, 1);
    float3 worldNormal = normalize(mul(tangentSpaceNormal, float3x3(surfaceTangent, surfaceBitangent, surfaceNormal)));

    if (Debug.x > 0)
    {
        const float3 lodColors[8] = {
            float3(0.2, 0.8, 0.2),
            float3(0.8, 0.8, 0.1),
            float3(0.8, 0.3, 0.1),
            float3(0.2, 0.5, 0.9),
            float3(0.8, 0.2, 0.8),
            float3(0.2, 0.8, 0.8),
            float3(0.9, 0.9, 0.9),
            float3(0.5, 0.5, 0.5)
        };
        albedo = lodColors[(int)Debug.y];
    }

    Material material = {float4(albedo, 1), float3(0.04, 0.04, 0.04), 1 - roughness};
    float4 lighting = ComputeLighting(gLights, material, input.worldPosition, worldNormal, normalize(gEyePosW - input.worldPosition), 1);
    return float4(gAmbientLight.rgb * albedo + lighting.rgb, 1);
}

float4 VSBounds(uint vertexIndex : SV_VertexID) : SV_POSITION
{
    const uint edgeCorners[24] = {0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7};
    uint cornerIndex = edgeCorners[vertexIndex];
    float3 cornerPosition = float3(
        cornerIndex & 1 ? BoundsMax.x : BoundsMin.x,
        cornerIndex & 2 ? BoundsMax.y : BoundsMin.y,
        cornerIndex & 4 ? BoundsMax.z : BoundsMin.z);
    return mul(float4(cornerPosition, 1), gViewProj);
}

float4 PSBounds() : SV_Target
{
    return float4(1, 0.7, 0.05, 1);
}

#ifdef EDITOR_VIEWPORT
cbuffer EditorSelection : register(b3)
{
    uint EntityIdentifier;
};
struct EditorPixelOutput
{
    float4 color : SV_Target0;
    uint identifier : SV_Target1;
};
EditorPixelOutput PSTerrain(TerrainVertexOutput input)
{
    EditorPixelOutput output;
    output.color = ShadeSurface(input);
    output.identifier = EntityIdentifier;
    return output;
}
#else
float4 PSTerrain(TerrainVertexOutput input) : SV_Target0
{
    return ShadeSurface(input);
}
#endif
