#include "GiCommon.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;
    if (pixel.x >= (uint)gScreenSize.x || pixel.y >= (uint)gScreenSize.y) return;
    float4 direct = gSceneColor.Load(int3(pixel, 0));
    float depth = gSceneDepth.Load(int3(pixel, 0));
    float3 diffuse = gDiffuseWeight.Load(int3(pixel, 0)).rgb;
    if (depth >= 0.99999 || max(diffuse.r, max(diffuse.g, diffuse.b)) <= 0.00001)
    {
        gOutput[pixel] = direct;
        return;
    }

    uint halfWidth, halfHeight;
    gGiHalf.GetDimensions(halfWidth, halfHeight);
    float3 normal = normalize(gSceneNormal.Load(int3(pixel, 0)).xyz);
    float3 center = ViewPosition(PixelUV(pixel), depth);
    float3 indirect = 0;
    float total = 0;
    // Four depth/normal aware neighbors limit half-resolution color leaking.
    [unroll] for (int y = 0; y < 2; ++y)
    [unroll] for (int x = 0; x < 2; ++x)
    {
        int2 halfPixel = clamp(int2(pixel / 2) + int2(x - (1 - int(pixel.x & 1)), y - (1 - int(pixel.y & 1))),
            int2(0, 0), int2(halfWidth - 1, halfHeight - 1));
        uint2 sourcePixel = min(uint2(halfPixel) * 2 + 1, uint2(gScreenSize.xy) - 1);
        float sourceDepth = gSceneDepth.Load(int3(sourcePixel, 0));
        if (sourceDepth >= 0.99999) continue;
        float3 sourcePosition = ViewPosition(PixelUV(sourcePixel), sourceDepth);
        float3 sourceNormal = normalize(gSceneNormal.Load(int3(sourcePixel, 0)).xyz);
        float similarity = pow(saturate(dot(normal, sourceNormal)), 8.0) /
            (1.0 + abs(center.z - sourcePosition.z) * 8.0);
        indirect += gGiHalf.Load(int3(halfPixel, 0)).rgb * similarity;
        total += similarity;
    }
    if (total > 0.00001) indirect /= total;
    gOutput[pixel] = float4(max(direct.rgb + indirect * diffuse * gControl.z, 0.0), direct.a);
}
