#include "LightingUtil.hlsl"
#define InverseLuminance false


Texture2D gHistory : register(t0);
Texture2D gCurrent : register(t1);
Texture2D gVelocity : register(t2);


SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearClamp : register(s3);

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(uint vid : SV_VertexID)
{
    VertexOut vout;
    
    vout.TexC = float2(vid & 1, (vid & 2) >> 1);
    vout.PosH = float4(vout.TexC * float2(4, -4) + float2(-1, 1), 0, 1);

    return vout;
}
struct PSOutput
{
    float4 RT0 : SV_Target0;
};

float4 AdjustHDRColor (float4 color)
{
    if (InverseLuminance)
    {
        float luminance = dot(color.rgb, float3(0.299, 0.587, 0.114));
        float luminanceWeight = 1.0 / (1.0 + luminance);
        return float4(color.rgb, 1.0) * luminanceWeight;
    }
    else //log
    return float4(color.rgb > 0.0 ? log(color.rgb) : -100.0, 1.0);
}


static const float PI = 3.14159265359;
static const float G = 9.81;
float JONSWAP(float2 k, float omega_p)
{
    float k_len = length(k);

    // защита от деления на 0
    if (k_len < 1e-6)
        return 0.0;

    // дисперсионное соотношение (deep water)
    float omega = sqrt(G * k_len);

    // параметры JONSWAP
    float alpha = 0.0081;
    float gamma = 3.3;

    // sigma зависит от того, меньше или больше пика
    float sigma = (omega <= omega_p) ? 0.07 : 0.09;

    // базовый PM-спектр
    float pm =
        alpha * G * G /
        pow(omega, 5.0) *
        exp(-1.25 * pow(omega_p / omega, 4.0));

    // усиление пика
    float exponent =
        exp(-pow(omega - omega_p, 2.0) /
            (2.0 * sigma * sigma * omega_p * omega_p));

    float jonswap = pm * pow(gamma, exponent);
    
    float2 wind_dir = normalize(float2(0.5, 0.5));
    float wind_exp = 3.5;
    
    float D = pow(max(dot(normalize(k), wind_dir), 0.0), wind_exp);
    jonswap *= D;

    return jonswap;
}


PSOutput PS(VertexOut pin)
{
    int x;
    int y;
    
    gCurrent.GetDimensions(x, y);
    int2 size = int2(x, y);
    float2 uv = pin.PosH.xy / size;
    PSOutput output;

    float4 currentColor = AdjustHDRColor(gCurrent.Load(int3(pin.PosH.xy, 0)));
    float2 velocity = gVelocity.Load(int3(pin.PosH.xy, 0)).xy;
    float2 historyUV = uv - velocity; 

    int2 historyPix = int2(historyUV);

    float4 historyColor = AdjustHDRColor(gHistory.Sample(gsamPointClamp, historyUV));
    float4 minColor = currentColor;
    float4 maxColor = minColor;

    for (int i = -1; i <= 1; i++)
        {
            for (int j = -1; j <= 1; j++)
            {
                float4 color = AdjustHDRColor(gCurrent.Load(int3(pin.PosH.xy, 0) + int3(i, j, 0)));
                minColor = min(minColor, color);
                maxColor = max(maxColor, color);

            }
        }
    float4 ClampedColor = clamp(historyColor, minColor, maxColor);
    float weightHistory = 0.9 * historyColor.a;
    float weightCurr = 0.1 * currentColor.a;

    float4 blendedColor = ClampedColor * weightHistory + currentColor * weightCurr;
    blendedColor /= weightHistory + weightCurr;
    output.RT0 = blendedColor;
    
    float2 k = (2.0 * pin.TexC - 0.5) * 0.5;
    float omega_p = 2.0 * PI / 8.0;
    
    if (!InverseLuminance)
    {
        output.RT0 = float4(exp(output.RT0.rgb), blendedColor.a);
        //float s = sqrt(JONSWAP(k, omega_p)/2.0);
        //output.RT0 = float4(s, s, 0.0, 1.0);
    }
    return output;
    
}
