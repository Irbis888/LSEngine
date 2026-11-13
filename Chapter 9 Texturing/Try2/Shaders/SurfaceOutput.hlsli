float4 ShadePixel(SurfaceInterpolants input, bool frontFace)
{
    const float4 baseColor = ReadBaseColor(input.uv);
#ifdef SURFACE_MASKED
    clip(baseColor.a - AlphaCutoff);
#endif
    return float4(EncodeSRGB(ShadeSurface(input, frontFace, baseColor.rgb)), baseColor.a);
}

#ifdef EDITOR_VIEWPORT
cbuffer SurfaceSelection : register(b3)
{
    uint EntityIdentifier;
};

struct SurfaceOutput
{
    float4 color : SV_Target0;
    uint identifier : SV_Target1;
};

SurfaceOutput PS(SurfaceInterpolants input, bool frontFace : SV_IsFrontFace)
{
    SurfaceOutput output;
    output.color = ShadePixel(input, frontFace);
    output.identifier = EntityIdentifier;
    return output;
}
#else
float4 PS(SurfaceInterpolants input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
    return ShadePixel(input, frontFace);
}
#endif
