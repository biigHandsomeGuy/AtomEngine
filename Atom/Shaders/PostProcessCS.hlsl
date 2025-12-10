#include "Common.hlsli"

Texture2D ScreenTexture : register(t0);
RWTexture2D<float4> OutTexture : register(u0);

cbuffer MaterialConstants : register(b0)
{
    float exposure;
    bool UseFXAA;
    bool UseReinhard;
    bool UseFilmic;
    bool UseAces;
}

#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/24.0)
#define FXAA_SEARCH_STEPS        32
#define FXAA_SEARCH_ACCELERATION 1
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX              1
#define FXAA_SUBPIX_FASTER       0
#define FXAA_SUBPIX_CAP          (3.0/4.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#define FXAA_SUBPIX_TRIM_SCALE (1.0/(1.0 - FXAA_SUBPIX_TRIM))

float4 FxaaTexOff(Texture2D ScreenTexture, float2 pos, int2 off)
{
    return ScreenTexture.SampleLevel(gsamAnisotropicClamp, pos.xy, 0.0, off.xy);
}

float GetLuminance(float3 rgb)
{
    return rgb.r * 0.2126 + rgb.g * 0.7152 + rgb.b * 0.0722;
}

float3 FxaaFilterReturn(float3 rgb)
{
    return rgb;
}

float4 FxaaTexGrad(Texture2D ScreenTexture, float2 pos, float2 grad)
{
    return ScreenTexture.SampleGrad(gsamAnisotropicClamp, pos.xy, grad, grad);
}

float3 FxaaLerp3(float3 a, float3 b, float amountOfA)
{
    return (float3(-amountOfA, 0, 0) * b) +
        ((a * float3(amountOfA, 0, 0)) + b);
}

float4 FxaaTexLod0(Texture2D ScreenTexture, float2 pos)
{
    return ScreenTexture.SampleLevel(gsamAnisotropicClamp, pos.xy, 0.0);
}

float4 Reinhard(float4 color)
{
    return color / (color + 1);
}

float4 Filmic(float4 col)
{
    col *= 0.6 * exposure;
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;

    return ((col * (A * col + C * B) + D * E) /
           (col * (A * col + B) + D * F)) - E / F;
}

float4 ACESFilm(float4 x)
{
    x *= 0.18 * exposure;
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{

    uint width, height;
    ScreenTexture.GetDimensions(width, height);

    uint2 pixel = dispatchThreadID.xy;
    if (pixel.x >= width || pixel.y >= height)
        return;

    float2 screenSize = float2(width, height);
    float2 rcpFrame = 1.0 / screenSize;


    float2 uv = (float2(pixel) + 0.5f) / screenSize;

    float4 sceneColor = ScreenTexture.Sample(gsamAnisotropicClamp, uv);
    float4 color = sceneColor;

    if (UseFXAA)
    {
        // SEARCH MAP
        float3 rgbN = FxaaTexOff(ScreenTexture, uv, int2(0, -1)).xyz;
        float3 rgbW = FxaaTexOff(ScreenTexture, uv, int2(-1, 0)).xyz;
        float3 rgbM = FxaaTexOff(ScreenTexture, uv, int2(0, 0)).xyz;
        float3 rgbE = FxaaTexOff(ScreenTexture, uv, int2(1, 0)).xyz;
        float3 rgbS = FxaaTexOff(ScreenTexture, uv, int2(0, 1)).xyz;

        float lumaN = GetLuminance(rgbN);
        float lumaW = GetLuminance(rgbW);
        float lumaM = GetLuminance(rgbM);
        float lumaE = GetLuminance(rgbE);
        float lumaS = GetLuminance(rgbS);

        float rangeMin = min(lumaM, min(min(lumaN, lumaW), min(lumaS, lumaE)));
        float rangeMax = max(lumaM, max(max(lumaN, lumaW), max(lumaS, lumaE)));
        float range = rangeMax - rangeMin;

        if (range < max(FXAA_EDGE_THRESHOLD_MIN, rangeMax * FXAA_EDGE_THRESHOLD))
        {
            color = float4(FxaaFilterReturn(rgbM), 1.0f);
        }
        else
        {
            float3 rgbL = rgbN + rgbW + rgbM + rgbE + rgbS;

#if FXAA_SUBPIX != 0
            float lumaL = (lumaN + lumaW + lumaE + lumaS) * 0.25;
            float rangeL = abs(lumaL - lumaM);
#endif
#if FXAA_SUBPIX == 1
            float blendL = max(0.0,
                (rangeL / range) - FXAA_SUBPIX_TRIM) * FXAA_SUBPIX_TRIM_SCALE;
            blendL = min(FXAA_SUBPIX_CAP, blendL);
#endif

            // CHOOSE VERTICAL OR HORIZONTAL SEARCH
            float3 rgbNW = FxaaTexOff(ScreenTexture, uv, int2(-1, -1)).xyz;
            float3 rgbNE = FxaaTexOff(ScreenTexture, uv, int2(1, -1)).xyz;
            float3 rgbSW = FxaaTexOff(ScreenTexture, uv, int2(-1, 1)).xyz;
            float3 rgbSE = FxaaTexOff(ScreenTexture, uv, int2(1, 1)).xyz;

#if (FXAA_SUBPIX_FASTER == 0) && (FXAA_SUBPIX > 0)
            rgbL += (rgbNW + rgbNE + rgbSW + rgbSE);
            rgbL *= float3(1.0 / 9.0, 0, 0);
#endif

            float lumaNW = GetLuminance(rgbNW);
            float lumaNE = GetLuminance(rgbNE);
            float lumaSW = GetLuminance(rgbSW);
            float lumaSE = GetLuminance(rgbSE);

            float edgeVert =
                abs((0.25 * lumaNW) + (-0.5 * lumaN) + (0.25 * lumaNE)) +
                abs((0.50 * lumaW) + (-1.0 * lumaM) + (0.50 * lumaE)) +
                abs((0.25 * lumaSW) + (-0.5 * lumaS) + (0.25 * lumaSE));

            float edgeHorz =
                abs((0.25 * lumaNW) + (-0.5 * lumaW) + (0.25 * lumaSW)) +
                abs((0.50 * lumaN) + (-1.0 * lumaM) + (0.50 * lumaS)) +
                abs((0.25 * lumaNE) + (-0.5 * lumaE) + (0.25 * lumaSE));

            bool horzSpan = edgeHorz >= edgeVert;
            float lengthSign = horzSpan ? -rcpFrame.y : -rcpFrame.x;

            if (!horzSpan)
                lumaN = lumaW;
            if (!horzSpan)
                lumaS = lumaE;

            float gradientN = abs(lumaN - lumaM);
            float gradientS = abs(lumaS - lumaM);

            lumaN = (lumaN + lumaM) * 0.5;
            lumaS = (lumaS + lumaM) * 0.5;

            // CHOOSE SIDE OF PIXEL WHERE GRADIENT IS HIGHEST
            bool pairN = gradientN >= gradientS;
            if (!pairN)
                lumaN = lumaS;
            if (!pairN)
                gradientN = gradientS;
            if (!pairN)
                lengthSign *= -1.0;

            float2 posN;
            posN.x = uv.x + (horzSpan ? 0.0 : lengthSign * 0.5);
            posN.y = uv.y + (horzSpan ? lengthSign * 0.5 : 0.0);

            // CHOOSE SEARCH LIMITING VALUES
            gradientN *= FXAA_SEARCH_THRESHOLD;

            // SEARCH IN BOTH DIRECTIONS
            float2 posP = posN;
            float2 offNP = horzSpan ?
                float2(rcpFrame.x, 0.0) :
                float2(0.0f, rcpFrame.y);
            float lumaEndN = lumaN;
            float lumaEndP = lumaN;
            bool doneN = false;
            bool doneP = false;

#if FXAA_SEARCH_ACCELERATION == 1
            posN += offNP * float2(-1.0, -1.0);
            posP += offNP * float2(1.0, 1.0);
#endif

            for (int i = 0; i < FXAA_SEARCH_STEPS; i++)
            {
#if FXAA_SEARCH_ACCELERATION == 1
                if (!doneN)
                    lumaEndN = GetLuminance(FxaaTexLod0(ScreenTexture, posN.xy).xyz);
                if (!doneP)
                    lumaEndP = GetLuminance(FxaaTexLod0(ScreenTexture, posP.xy).xyz);
#endif
                doneN = doneN || (abs(lumaEndN - lumaN) >= gradientN);
                doneP = doneP || (abs(lumaEndP - lumaN) >= gradientN);
                if (doneN && doneP)
                    break;
                if (!doneN)
                    posN -= offNP;
                if (!doneP)
                    posP += offNP;
            }

            // HANDLE IF CENTER IS ON POSITIVE OR NEGATIVE SIDE
            float dstN = horzSpan ? uv.x - posN.x : uv.y - posN.y;
            float dstP = horzSpan ? posP.x - uv.x : posP.y - uv.y;
            bool directionN = dstN < dstP;
            lumaEndN = directionN ? lumaEndN : lumaEndP;

            // CHECK IF PIXEL IS IN SECTION OF SPAN WHICH GETS NO FILTERING
            if (((lumaM - lumaN) < 0.0) == ((lumaEndN - lumaN) < 0.0))
                lengthSign = 0.0;

            float spanLength = (dstP + dstN);
            dstN = directionN ? dstN : dstP;
            float subPixelOffset = (0.5 + (dstN * (-1.0 / spanLength))) * lengthSign;
            float3 rgbF = FxaaTexLod0(ScreenTexture, float2(
                uv.x + (horzSpan ? 0.0 : subPixelOffset),
                uv.y + (horzSpan ? subPixelOffset : 0.0))).xyz;

            color = float4(FxaaFilterReturn(FxaaLerp3(rgbL, rgbF, blendL)), 1.0f);
        }
    }

    if (UseReinhard)
        color = Reinhard(color);
    if (UseFilmic)
        color = Filmic(color);
    if (UseAces)
        color = ACESFilm(color);

    color = pow(color, 1 / 2.2);

    OutTexture[pixel] = color;
}