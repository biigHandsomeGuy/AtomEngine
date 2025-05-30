#include "Common.hlsli"


Texture2D ScreenTexture : register(t20); 
//Texture2D BloomTexture : register(t18); 
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


struct PSInput
{
	float4 Position : SV_POSITION;
	float2 uv : TEXCOORD;
};

float4 Reinhard(float4 color)
{
    return color / (color + 1);
}
float4 Filmic(float4 col)
{
    col *= 0.6;
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;

    return ((col * (A * col + C * B) + D * E) / (col * (A * col + B) + D * F)) - E / F;
}

float4 ACESFilm(float4 x)
{
    x *= 0.1;
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
float4 main(PSInput input) : SV_Target
{
    //uint order = GetRasterizationOrder();
    // return float4((uint3(order >> uint3(0, 8, 16)) & 0xFF) / 255.0f, 1);
	float2 ScreenTextureSize;
	ScreenTexture.GetDimensions(ScreenTextureSize.x, ScreenTextureSize.y);
	
	float2 rcpFrame = 1.0 / ScreenTextureSize;
	float4 sceneColor = ScreenTexture.Sample(gsamAnisotropicClamp, input.uv);
	//float4 bloomColor = BloomTexture.Sample(gsamAnisotropicClamp, input.uv);
	float4 color = sceneColor;
   
    if (UseFXAA)
	{
		//SEARCH MAP
        float3 rgbN = FxaaTexOff(ScreenTexture, input.uv.xy, int2(0, -1)).xyz;
        float3 rgbW = FxaaTexOff(ScreenTexture, input.uv.xy, int2(-1, 0)).xyz;
        float3 rgbM = FxaaTexOff(ScreenTexture, input.uv.xy, int2(0, 0)).xyz;
        float3 rgbE = FxaaTexOff(ScreenTexture, input.uv.xy, int2(1, 0)).xyz;
        float3 rgbS = FxaaTexOff(ScreenTexture, input.uv.xy, int2(0, 1)).xyz;
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
        float3 rgbL = rgbN + rgbW + rgbM + rgbE + rgbS;
	
	//COMPUTE LOWPASS
#if FXAA_SUBPIX != 0
        float lumaL = (lumaN + lumaW + lumaE + lumaS) * 0.25;
        float rangeL = abs(lumaL - lumaM);
#endif
#if FXAA_SUBPIX == 1
        float blendL = max(0.0,
			(rangeL / range) - FXAA_SUBPIX_TRIM) * FXAA_SUBPIX_TRIM_SCALE;
        blendL = min(FXAA_SUBPIX_CAP, blendL);
#endif
	
	
	//CHOOSE VERTICAL OR HORIZONTAL SEARCH
        float3 rgbNW = FxaaTexOff(ScreenTexture, input.uv.xy, int2(-1, -1)).xyz;
        float3 rgbNE = FxaaTexOff(ScreenTexture, input.uv.xy, int2(1, -1)).xyz;
        float3 rgbSW = FxaaTexOff(ScreenTexture, input.uv.xy, int2(-1, 1)).xyz;
        float3 rgbSE = FxaaTexOff(ScreenTexture, input.uv.xy, int2(1, 1)).xyz;
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
	
	
	//CHOOSE SIDE OF PIXEL WHERE GRADIENT IS HIGHEST
        bool pairN = gradientN >= gradientS;
        if (!pairN)
            lumaN = lumaS;
        if (!pairN)
            gradientN = gradientS;
        if (!pairN)
            lengthSign *= -1.0;
        float2 posN;
        posN.x = input.uv.x + (horzSpan ? 0.0 : lengthSign * 0.5);
        posN.y = input.uv.y + (horzSpan ? lengthSign * 0.5 : 0.0);
	
	//CHOOSE SEARCH LIMITING VALUES
        gradientN *= FXAA_SEARCH_THRESHOLD;

	//SEARCH IN BOTH DIRECTIONS UNTIL FIND LUMA PAIR AVERAGE IS OUT OF RANGE
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
                lumaEndN =
				GetLuminance(FxaaTexLod0(ScreenTexture, posN.xy).xyz);
            if (!doneP)
                lumaEndP =
				GetLuminance(FxaaTexLod0(ScreenTexture, posP.xy).xyz);
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
	
	
	//HANDLE IF CENTER IS ON POSITIVE OR NEGATIVE SIDE
        float dstN = horzSpan ? input.uv.x - posN.x : input.uv.y - posN.y;
        float dstP = horzSpan ? posP.x - input.uv.x : posP.y - input.uv.y;
        bool directionN = dstN < dstP;
        lumaEndN = directionN ? lumaEndN : lumaEndP;
	
	//CHECK IF PIXEL IS IN SECTION OF SPAN WHICH GETS NO FILTERING   
        if (((lumaM - lumaN) < 0.0) == ((lumaEndN - lumaN) < 0.0)) 
            lengthSign = 0.0;
	
        float spanLength = (dstP + dstN);
        dstN = directionN ? dstN : dstP;
        float subPixelOffset = (0.5 + (dstN * (-1.0 / spanLength))) * lengthSign;
        float3 rgbF = FxaaTexLod0(ScreenTexture, float2(
	input.uv.x + (horzSpan ? 0.0 : subPixelOffset),
	input.uv.y + (horzSpan ? subPixelOffset : 0.0))).xyz;
	
        color = float4(FxaaFilterReturn(FxaaLerp3(rgbL, rgbF, blendL)), 1.0f);

    }
    if (UseReinhard)
        color = Reinhard(color);
    if(UseFilmic)
        color = Filmic(color);
    if(UseAces)
        color = ACESFilm(color);
    //color = color / (1.0f + color);
    
    
    
	color = pow(color, 1 / 2.2);

	return color;
}
