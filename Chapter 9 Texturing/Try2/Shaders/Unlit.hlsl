#include "Surface.hlsli"

float3 ShadeSurface(SurfaceInterpolants input, bool frontFace, float3 baseColor)
{
    return baseColor + ReadEmissive(input.uv);
}

#include "SurfaceOutput.hlsli"
