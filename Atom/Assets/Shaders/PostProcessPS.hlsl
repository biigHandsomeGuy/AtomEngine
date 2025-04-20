#include "Common.hlsli"

#define fixedThreshold 0.0312
#define relativeThreshold  0.063
#define subpixelBlending 0.75f

#define EXTRA_EDGE_STEPS 10
#define EDGE_STEP_SIZES 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0
#define LAST_EDGE_STEP_GUESS 8.0
static const float edgeStepSizes[EXTRA_EDGE_STEPS] = { EDGE_STEP_SIZES };
Texture2D ScreenTexture : register(t17); 
Texture2D BloomTexture : register(t18); 
cbuffer MaterialConstants : register(b0)
{
    float exposure;
    bool FXAA;
}

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

float Luminance(float3 color)
{
    return pow(color.r * 0.299 + color.g * 0.587 + color.b * 0.114, 1);
}

float GetLuma(float2 uv, float uOffset = 0.0, float vOffset = 0.0)
{
    uv += float2(uOffset, vOffset);
    return Luminance(ScreenTexture.Sample(gsamLinearClamp, uv).rgb);
}

struct LumaNeighborhood
{
    float m, n, s, w, e, ne, se, sw, nw;
    float highest, lowest, range;
};


LumaNeighborhood GetLumaNeighborhood(float2 uv, float2 uvOffset)
{
    LumaNeighborhood luma;
    luma.m = GetLuma(uv);
    luma.n = GetLuma(uv, 0.0, -uvOffset.y);
    luma.e = GetLuma(uv, uvOffset.x, 0.0);
    luma.s = GetLuma(uv, 0.0, uvOffset.y);
    luma.w = GetLuma(uv, -uvOffset.x, 0.0);
    luma.ne = GetLuma(uv, uvOffset.x, -uvOffset.y);
    luma.se = GetLuma(uv, uvOffset.x, uvOffset.y);
    luma.sw = GetLuma(uv, -uvOffset.x, uvOffset.y);
    luma.nw = GetLuma(uv, -uvOffset.x, +uvOffset.y);

    luma.highest = max(max(max(max(luma.m, luma.n), luma.e), luma.s), luma.w);
    luma.lowest = min(min(min(min(luma.m, luma.n), luma.e), luma.s), luma.w);
    luma.range = luma.highest - luma.lowest;
    return luma;
}
bool CanSkipFXAA(LumaNeighborhood luma)
{
    return luma.range < fixedThreshold;
}

float GetSubpixelBlendFactor(LumaNeighborhood luma)
{
    float filter = 2.0 * (luma.n + luma.e + luma.s + luma.w);
    filter += luma.ne + luma.nw + luma.se + luma.sw;
    filter *= 1.0 / 12.0;
    filter = abs(filter - luma.m);
    filter = saturate(filter / luma.range);
    filter = smoothstep(0, 1, filter);
    return filter * filter * subpixelBlending;
}
bool IsHorizontalEdge(LumaNeighborhood luma)
{
    float horizontal =
		2.0 * abs(luma.n + luma.s - 2.0 * luma.m) +
		abs(luma.ne + luma.se - 2.0 * luma.e) +
		abs(luma.nw + luma.sw - 2.0 * luma.w);
    float vertical =
		2.0 * abs(luma.e + luma.w - 2.0 * luma.m) +
		abs(luma.ne + luma.nw - 2.0 * luma.n) +
		abs(luma.se + luma.sw - 2.0 * luma.s);
    return horizontal >= vertical;
}
struct FXAAEdge
{
    bool isHorizontal;
    float pixelStep;
    float lumaGradient, otherLuma;
};

float4 GetSource(float2 uv)
{
    return ScreenTexture.Sample(gsamLinearClamp, uv);
}

float2 GetSourceTexelSize()
{
    float2 ScreenTextureSize;
    ScreenTexture.GetDimensions(ScreenTextureSize.x, ScreenTextureSize.y);
    return 1/ScreenTextureSize;
}

FXAAEdge GetFXAAEdge(LumaNeighborhood luma)
{
    float lumaP, lumaN;
    FXAAEdge edge;
    edge.isHorizontal = IsHorizontalEdge(luma);
    if (edge.isHorizontal)
    {
        edge.pixelStep = -GetSourceTexelSize().y;
        lumaP = luma.n;
        lumaN = luma.s;
    }
	else 
    {
        edge.pixelStep = GetSourceTexelSize().x;
        lumaP = luma.e;
        lumaN = luma.w;
    }
    float gradientP = abs(lumaP - luma.m);
    float gradientN = abs(lumaN - luma.m);

    if (gradientP < gradientN)
    {
        edge.pixelStep = -edge.pixelStep;
        edge.lumaGradient = gradientN;
        edge.otherLuma = lumaN;
    }
    else
    {
        edge.lumaGradient = gradientP;
        edge.otherLuma = lumaP;
    }
    return edge;
}
float GetEdgeBlendFactor(LumaNeighborhood luma, FXAAEdge edge, float2 uv)
{
    float2 edgeUV = uv;
    float2 uvStep = 0.0;
    if (edge.isHorizontal)
    {
        edgeUV.y += 0.5 * edge.pixelStep;
        uvStep.x = GetSourceTexelSize().x;
    }
    else
    {
        edgeUV.x += 0.5 * edge.pixelStep;
        uvStep.y = GetSourceTexelSize().y;
    }

    float edgeLuma = 0.5 * (luma.m + edge.otherLuma);
    float gradientThreshold = 0.25 * edge.lumaGradient;
			
    float2 uvP = edgeUV + uvStep;
    float lumaDeltaP = GetLuma(uvP) - edgeLuma;
    bool atEndP = abs(lumaDeltaP) >= gradientThreshold;

    int i;
    

    for (i = 0; i < EXTRA_EDGE_STEPS && !atEndP; i++)
    {
        uvP += uvStep * edgeStepSizes[i];
        lumaDeltaP = GetLuma(uvP) - edgeLuma;
        atEndP = abs(lumaDeltaP) >= gradientThreshold;
    }
    if (!atEndP)
    {
        uvP += uvStep * LAST_EDGE_STEP_GUESS;
    }

    float2 uvN = edgeUV - uvStep;
    float lumaDeltaN = GetLuma(uvN) - edgeLuma;
    bool atEndN = abs(lumaDeltaN) >= gradientThreshold;

    
    for (i = 0; i < EXTRA_EDGE_STEPS && !atEndN; i++)
    {
        uvN -= uvStep * edgeStepSizes[i];
        lumaDeltaN = GetLuma(uvN) - edgeLuma;
        atEndN = abs(lumaDeltaN) >= gradientThreshold;
    }
    if (!atEndN)
    {
        uvN -= uvStep * LAST_EDGE_STEP_GUESS;
    }

    float distanceToEndP, distanceToEndN;
    if (edge.isHorizontal)
    {
        distanceToEndP = uvP.x - uv.x;
        distanceToEndN = uv.x - uvN.x;
    }
    else
    {
        distanceToEndP = uvP.y - uv.y;
        distanceToEndN = uv.y - uvN.y;
    }

    float distanceToNearestEnd;
    bool deltaSign;
    if (distanceToEndP <= distanceToEndN)
    {
        distanceToNearestEnd = distanceToEndP;
        deltaSign = lumaDeltaP >= 0;
    }
    else
    {
        distanceToNearestEnd = distanceToEndN;
        deltaSign = lumaDeltaN >= 0;
    }

    if (deltaSign == (luma.m - edgeLuma >= 0))
    {
        return 0.0;
    }
    else
    {
        return 0.5 - distanceToNearestEnd / (distanceToEndP + distanceToEndN);
    }
}
float4 main(PSInput input) : SV_Target
{
    float2 ScreenTextureSize;
    ScreenTexture.GetDimensions(ScreenTextureSize.x, ScreenTextureSize.y);
    
    float2 texelSize = 1.0 / ScreenTextureSize;
    float4 sceneColor = ScreenTexture.Sample(gsamLinearClamp, input.TexCoord);
    float4 bloomColor = BloomTexture.Sample(gsamLinearClamp, input.TexCoord);
    float4 color = sceneColor;
    
    if(FXAA)
    {
        LumaNeighborhood luma = GetLumaNeighborhood(input.TexCoord, texelSize);
        
        if (CanSkipFXAA(luma))
        {
            
            color = GetSource(input.TexCoord);

        }
        else
        {
            FXAAEdge edge = GetFXAAEdge(luma);

            float blendFactor = max(
		GetSubpixelBlendFactor(luma), GetEdgeBlendFactor(luma, edge, input.TexCoord)
	);
            float2 blendUV = input.TexCoord;
            if (edge.isHorizontal)
            {
                blendUV.y += blendFactor * edge.pixelStep;
            }
            else
            {
                blendUV.x += blendFactor * edge.pixelStep;
            }
            
            color = GetSource(blendUV);
        }

    }

    
    color = 1 - exp(-color * exposure);   
    
    color = pow(color, 1 / 2.2);

    return color;

    
    
}
