#include "LightingUtil.hlsl"

Texture2D<float4> BaseColorMap : register(t0);
Texture2D<float4> NormalMap : register(t1);
Texture2D<float4> ORMMap : register(t2);
Texture2D<float4> EmissiveMap : register(t3);
SamplerState SurfaceSampler : register(s4);

cbuffer SurfaceObject : register(b0)
{
    float4x4 World;
    float4x4 InverseWorld;
    float4x4 ObjectUV;
};

cbuffer SurfaceFrame : register(b1)
{
    float4x4 View;
    float4x4 InverseView;
    float4x4 Projection;
    float4x4 InverseProjection;
    float4x4 ViewProjection;
    float4x4 InverseViewProjection;
    float3 EyePosition;
    float FramePadding;
    float2 RenderSize;
    float2 InverseRenderSize;
    float NearPlane;
    float FarPlane;
    float TotalTime;
    float DeltaTime;
    float4 AmbientLight;
    Light Lights[MaxLights];
};

cbuffer SurfaceMaterial : register(b2)
{
    float4 BaseColorFactor;
    float3 EmissiveColor;
    float EmissiveIntensity;
    float2 UVScale;
    float2 UVOffset;
    float RoughnessFactor;
    float MetallicFactor;
    float NormalStrength;
    float AOStrength;
    float AlphaCutoff;
    uint TextureFlags;
    float2 MaterialPadding;
};

struct SurfaceVertex
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 uv : TEXCOORD;
};

struct SurfaceInterpolants
{
    float4 clipPosition : SV_POSITION;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 uv : TEXCOORD;
};

SurfaceInterpolants VS(SurfaceVertex vertex)
{
    SurfaceInterpolants result;
    const float4 worldPosition = mul(float4(vertex.position, 1), World);
    result.clipPosition = mul(worldPosition, ViewProjection);
    result.worldPosition = worldPosition.xyz;
    result.normal = mul(vertex.normal, transpose((float3x3)InverseWorld));
    result.tangent = mul(vertex.tangent, (float3x3)World);
    result.uv = mul(float4(vertex.uv, 0, 1), ObjectUV).xy * UVScale + UVOffset;
    return result;
}

float3 SafeUnit(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return isfinite(lengthSquared) && lengthSquared > 1e-12f
        ? value * rsqrt(max(lengthSquared, 1e-12f))
        : fallback;
}

float3 DecodeSRGB(float3 value)
{
    value = saturate(value);
    return lerp(value / 12.92f, pow((value + 0.055f) / 1.055f, 2.4f), step(0.04045f, value));
}

float3 EncodeSRGB(float3 value)
{
    value = saturate(value);
    return lerp(12.92f * value, 1.055f * pow(value, 1.0f / 2.4f) - 0.055f, step(0.0031308f, value));
}

float4 ReadBaseColor(float2 uv)
{
    float4 sampled = BaseColorMap.Sample(SurfaceSampler, uv);
    if ((TextureFlags & 1u) != 0)
    {
        sampled.rgb = DecodeSRGB(sampled.rgb);
    }
    return sampled * BaseColorFactor;
}

float3 ReadEmissive(float2 uv)
{
    float3 sampled = EmissiveMap.Sample(SurfaceSampler, uv).rgb;
    if ((TextureFlags & 2u) != 0)
    {
        sampled = DecodeSRGB(sampled);
    }
    return sampled * EmissiveColor * EmissiveIntensity;
}

float3 ReadWorldNormal(SurfaceInterpolants input, bool frontFace)
{
    float3 normal = SafeUnit(input.normal, float3(0, 1, 0));
    const float3 reference = abs(normal.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0);
    const float3 fallbackTangent = SafeUnit(cross(reference, normal), float3(1, 0, 0));
    const float3 tangent = SafeUnit(input.tangent - dot(input.tangent, normal) * normal, fallbackTangent);
    float3 bitangent = cross(normal, tangent);

    const float2 uvX = ddx(input.uv);
    const float2 uvY = ddy(input.uv);
    const float determinant = uvX.x * uvY.y - uvX.y * uvY.x;
    const float3 surfaceBitangent = (ddy(input.worldPosition) * uvX.x - ddx(input.worldPosition) * uvY.x) * sign(determinant);
    if (dot(bitangent, surfaceBitangent) < 0)
    {
        bitangent = -bitangent;
    }
    if (!frontFace)
    {
        normal = -normal;
        bitangent = -bitangent;
    }

    const float3 sampled = NormalMap.Sample(SurfaceSampler, input.uv).xyz;
    float3 tangentNormal = sampled * 2 - 1;
    if ((TextureFlags & 4u) != 0)
    {
        if ((TextureFlags & 8u) != 0)
        {
            tangentNormal.xy = sampled.xy;
        }
        tangentNormal.z = sqrt(saturate(1 - dot(tangentNormal.xy, tangentNormal.xy)));
    }
    tangentNormal.xy *= NormalStrength;
    tangentNormal.z = max(tangentNormal.z, 0.0f);
    return SafeUnit(tangent * tangentNormal.x + bitangent * tangentNormal.y + normal * tangentNormal.z, normal);
}
