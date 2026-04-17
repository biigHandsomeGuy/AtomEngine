#include "Common.hlsli"

Texture2D ScreenTexture : register(t0);
Texture2D HistoryTexture : register(t1);
Texture2D<float> DepthTexture : register(t2);
RWTexture2D<float4> OutTexture : register(u0);

cbuffer MaterialConstants : register(b0)
{
    float4x4 InvViewProj;
    float4x4 PrevViewProj;
    float2 CurrJitter;
    float2 PrevJitter;
    float exposure;
    bool UseFXAA;
    bool UseReinhard;
    bool UseFilmic;
    bool UseAces;
    bool UseTAA;
    bool TAAHistoryValid;
    float TAAFeedback;
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

float3 ApplyToneMapping(float3 inColor)
{
    float4 color = float4(inColor, 1.0f);
    if (UseReinhard)
        color = Reinhard(color);
    if (UseFilmic)
        color = Filmic(color);
    if (UseAces)
        color = ACESFilm(color);
    return max(color.rgb, 0.0f);
}

float2 ReprojectHistoryUv(float2 uv, float depth)
{
    float2 ndc;
    ndc.x = uv.x * 2.0 - 1.0;
    ndc.y = 1.0 - uv.y * 2.0;
    ndc -= CurrJitter;

    float4 currClip = float4(ndc, depth, 1.0);
    float4 worldPos = mul(InvViewProj, currClip);
    worldPos /= max(worldPos.w, 1e-6);

    float4 prevClip = mul(PrevViewProj, worldPos);
    if (prevClip.w <= 1e-6)
    {
        return float2(-1.0, -1.0);
    }
    float2 prevNdc = prevClip.xy / max(prevClip.w, 1e-6);
    prevNdc += PrevJitter;

    float2 prevUv;
    prevUv.x = prevNdc.x * 0.5 + 0.5;
    prevUv.y = 0.5 - prevNdc.y * 0.5;
    return prevUv;
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

    float3 sceneColor = ScreenTexture.Sample(gsamAnisotropicClamp, uv).rgb;
    float3 colorLinear = sceneColor;

    if (UseTAA && TAAHistoryValid)
    {
        float depth = DepthTexture.Load(int3(pixel, 0));
        float2 prevUv = ReprojectHistoryUv(uv, depth);
        bool validPrevUv = all(prevUv >= 0.0) && all(prevUv <= 1.0) && depth < 0.999999;
        float3 currToneMapped = ApplyToneMapping(sceneColor);
        float3 historyColor = currToneMapped;
        if (validPrevUv)
        {
            historyColor = HistoryTexture.Sample(gsamAnisotropicClamp, prevUv).rgb;
            historyColor = pow(saturate(historyColor), 2.2);
        }

        // Neighborhood clamp to reduce ghost trails without motion vectors.
        float3 minC = currToneMapped;
        float3 maxC = currToneMapped;
        float3 m1 = 0.0;
        float3 m2 = 0.0;
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                float3 n = ApplyToneMapping(FxaaTexOff(ScreenTexture, uv, int2(x, y)).rgb);
                minC = min(minC, n);
                maxC = max(maxC, n);
                m1 += n;
                m2 += n * n;
            }
        }
        if (validPrevUv)
        {
            historyColor = clamp(historyColor, minC, maxC);
            float3 mean = m1 / 9.0;
            float3 sigma = sqrt(max(m2 / 9.0 - mean * mean, 0.0));
            historyColor = clamp(historyColor, mean - 1.25 * sigma, mean + 1.25 * sigma);
        }

        const float2 motionPx2 = abs(prevUv - uv) * screenSize;
        const float motionPx = max(motionPx2.x, motionPx2.y);
        const float motionBlend = saturate(motionPx / 1.25f);

        const float lumaCurr = GetLuminance(currToneMapped);
        const float lumaHist = GetLuminance(historyColor);
        const float chromaDiff = max(max(abs(currToneMapped.r - historyColor.r), abs(currToneMapped.g - historyColor.g)), abs(currToneMapped.b - historyColor.b));
        const float lumaDiff = abs(lumaCurr - lumaHist);
        // Keep static regions stable: suppress reject when motion is near zero.
        const float rejectScale = lerp(0.20f, 1.0f, motionBlend);
        const float rejectBase = max(chromaDiff * 1.8f, lumaDiff * 2.0f);
        const float reject = validPrevUv ? saturate(rejectBase * rejectScale) : 1.0f;

        // Dual feedback: high accumulation when static, lower accumulation while moving.
        const float staticFeedback = saturate(max(TAAFeedback, 0.95f));
        const float dynamicFeedback = saturate(TAAFeedback * 0.60f);
        const float feedback = lerp(staticFeedback, dynamicFeedback, motionBlend);
        const float historyWeight = feedback * (1.0f - reject);
        colorLinear = lerp(currToneMapped, historyColor, historyWeight);
    }

    if (UseFXAA && !UseTAA)
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
            colorLinear = FxaaFilterReturn(rgbM);
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

            colorLinear = FxaaFilterReturn(FxaaLerp3(rgbL, rgbF, blendL));
        }
    }

    if (!UseTAA)
    {
        colorLinear = ApplyToneMapping(colorLinear);
    }

    float3 outColor = pow(max(colorLinear, 0.0f), 1.0 / 2.2);

    OutTexture[pixel] = float4(outColor, 1.0f);
}
