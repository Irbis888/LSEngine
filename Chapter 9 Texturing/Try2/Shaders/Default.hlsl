// Lit implementation of the shared mesh surface contract.
#include "Surface.hlsli"

float3 EvaluateBRDF(float3 baseColor, float metallic, float roughness,
    float3 normal, float3 view, float3 light, float3 radiance)
{
    const float normalLight = saturate(dot(normal, light));
    const float normalView = max(saturate(dot(normal, view)), 1e-4f);
    const float3 halfVector = SafeUnit(view + light, normal);
    const float normalHalf = saturate(dot(normal, halfVector));
    const float viewHalf = saturate(dot(view, halfVector));
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float distributionTerm = normalHalf * normalHalf * (alphaSquared - 1) + 1;
    const float distribution = alphaSquared / max(3.14159265f * distributionTerm * distributionTerm, 1e-12f);
    const float smithLight = normalView * sqrt(alphaSquared + (1 - alphaSquared) * normalLight * normalLight);
    const float smithView = normalLight * sqrt(alphaSquared + (1 - alphaSquared) * normalView * normalView);
    const float visibility = 0.5f / max(smithLight + smithView, 1e-5f);
    const float3 reflectance = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    const float3 fresnel = reflectance + (1 - reflectance) * pow(1 - viewHalf, 5);
    const float3 diffuse = (1 - fresnel) * (1 - metallic) * baseColor / 3.14159265f;
    return (diffuse + fresnel * distribution * visibility) * radiance * normalLight;
}

float3 ShadeSurface(SurfaceInterpolants input, bool frontFace, float3 baseColor)
{
    const float3 normal = ReadWorldNormal(input, frontFace);
    const float3 view = SafeUnit(EyePosition - input.worldPosition, normal);
    const float3 orm = ORMMap.Sample(SurfaceSampler, input.uv).rgb;
    const float roughness = clamp(RoughnessFactor * orm.g, 0.045f, 1.0f);
    const float metallic = saturate(MetallicFactor * orm.b);
    const float ao = lerp(1.0f, saturate(orm.r), AOStrength);
    float3 color = AmbientLight.rgb * baseColor * (1 - metallic) * ao;

    [unroll]
    for (int lightIndex = 0; lightIndex < 6; ++lightIndex)
    {
        const Light source = Lights[lightIndex];
        float3 lightDirection = SafeUnit(-source.Direction, normal);
        float attenuation = 1.0f;
        if (lightIndex >= 1)
        {
            const float3 offset = source.Position - input.worldPosition;
            const float distanceToLight = length(offset);
            lightDirection = SafeUnit(offset, normal);
            attenuation = saturate((source.FalloffEnd - distanceToLight) / max(source.FalloffEnd - source.FalloffStart, 1e-5f));
            if (lightIndex >= 3)
            {
                attenuation *= pow(max(dot(-lightDirection, source.Direction), 1e-6f), max(source.SpotPower, 0.0f));
            }
        }
        color += EvaluateBRDF(baseColor, metallic, roughness, normal, view, lightDirection, source.Strength * attenuation);
    }
    return color + ReadEmissive(input.uv);
}

#include "SurfaceOutput.hlsli"
