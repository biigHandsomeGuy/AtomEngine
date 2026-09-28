#include "GiCommon.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint halfWidth, halfHeight;
    gOutput.GetDimensions(halfWidth, halfHeight);
    if (dispatchId.x >= halfWidth || dispatchId.y >= halfHeight) return;

    uint2 center = min(dispatchId.xy * 2 + 1, uint2(gScreenSize.xy) - 1);
    float depth = gSceneDepth.Load(int3(center, 0));
    float3 weight = gDiffuseWeight.Load(int3(center, 0)).rgb;
    if (depth >= 0.99999 || max(weight.r, max(weight.g, weight.b)) <= 0.00001)
    {
        gOutput[dispatchId.xy] = 0;
        return;
    }

    float3 normal = normalize(gSceneNormal.Load(int3(center, 0)).xyz);
    float3 position = ViewPosition(PixelUV(center), depth);
    float3 tangent = normalize(cross(abs(normal.z) < 0.9 ? float3(0, 0, 1) : float3(0, 1, 0), normal));
    float3 bitangent = cross(normal, tangent);
    uint rayCount = (uint)gTrace.z;
    uint steps = (uint)gTrace.w;
    float3 incoming = 0;
    uint seed = dispatchId.x * 73856093u ^ dispatchId.y * 19349663u ^ (uint)gControl.w * 83492791u;

    [loop] for (uint ray = 0; ray < rayCount; ++ray)
    {
        float angle = 6.2831853 * (float(ray) + GiRandom(seed + 2 * ray)) / float(rayCount);
        float elevation = 0.2 + GiRandom(seed + 2 * ray + 1) * 0.7;
        float lateral = sqrt(1.0 - elevation * elevation);
        float3 direction = tangent * (cos(angle) * lateral) + bitangent * (sin(angle) * lateral) + normal * elevation;
        float3 origin = position + normal * max(0.025, gTrace.y * 0.25);
        [loop] for (uint step = 1; step <= steps; ++step)
        {
            float travel = gTrace.x * float(step) / float(steps);
            float3 probe = origin + direction * travel;
            if (probe.z >= -0.01) break;
            float2 uv = ProjectView(probe);
            if (any(uv <= 0.0) || any(uv >= 1.0)) break;
            uint2 pixel = min(uint2(uv * gScreenSize.xy), uint2(gScreenSize.xy) - 1);
            float hitDepth = gSceneDepth.Load(int3(pixel, 0));
            if (hitDepth >= 0.99999) continue;
            float3 hit = ViewPosition(PixelUV(pixel), hitDepth);
            float delta = probe.z - hit.z;
            // View forward is -Z. A ray crossing a surface gives negative delta.
            if (delta <= 0.0 && delta >= -gTrace.y)
            {
                float3 hitNormal = normalize(gSceneNormal.Load(int3(pixel, 0)).xyz);
                float facing = saturate(dot(hitNormal, -direction));
                float falloff = 1.0 - saturate(travel / gTrace.x);
                float3 radiance = min(max(gSceneColor.Load(int3(pixel, 0)).rgb, 0.0), 64.0);
                incoming += radiance * facing * elevation * falloff * falloff;
                break;
            }
        }
    }
    gOutput[dispatchId.xy] = float4(incoming * (2.0 / float(rayCount)), 1.0);
}
