
Texture2D gShadowMap : register(t0);

SamplerState gsamLinearWrap : register(s2);

SamplerComparisonState gsamShadow : register(s6);


cbuffer GlobalConstants : register(b1)
{
    float4x4 gView;
    float4x4 gProj;
    float4x4 gViewProj;
    float4x4 gSunShadowMatrix;
    float3 gCameraPos;
    float pad;
    float3 gSunPosition;
};

cbuffer ShaderParams : register(b2)
{
    float3 Albedo;
    int UseBasicShadow;
    
    int UsePCFShadow;
    int UsePCSSShadow;
    float LightWidth;
};


struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 ShadowPosH : POSITION0;
    float4 SsaoPosH : POSITION1;
    float3 PosW : POSITION2;
    float2 TexC : TEXCOORD;
    float3x3 tangentBasis : TAASIC;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
};



float Basic_ShadowCalculation(float4 shadowPosH)
{
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;

    // ranges[0,1]
    float3 projCoords = shadowPosH.xyz;

    // Depth in NDC space of current shading point
    float currentDepth = shadowPosH.z;
    
    float closestDepth = gShadowMap.Sample(gsamLinearWrap, shadowPosH.xy).r;
    float shadow = currentDepth > closestDepth ? 0 : 1;

    return 1 - shadow;
}
float PCF_ShadowCalculation(float4 shadowPosH)
{
     // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;

    // ranges[0,1]
    float3 projCoords = shadowPosH.xyz;

    // Depth in NDC space of current shading point
    float currentDepth = shadowPosH.z;
    
    float closestDepth = gShadowMap.Sample(gsamLinearWrap, shadowPosH.xy).r;
    float shadow = currentDepth > closestDepth ? 0 : 1;
    
    uint width, height, numMips;
    gShadowMap.GetDimensions(0, width, height, numMips);
    // Texel size.
    float texelSize = 1.0f / (float) width;

    float percentLit = 0.0f;

    [unroll]
    for (int y = -3; y <= 3; ++y)
    {
        for (int x = -3; x <= 3; ++x)
        {
            percentLit += gShadowMap.SampleCmpLevelZero(gsamShadow,
            shadowPosH.xy + float2(x,y) * texelSize, currentDepth).r;
        }
        
    }

    return 1 - percentLit / 49.0f;
}

float w_penumbra1(float d_receiver, float d_blocker, float w_light)
{
    return (d_receiver - d_blocker) * w_light / d_blocker;
}


float PCSS_ShadowCalculation(float4 shadowPosH, float bias)
{
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;
    // ranges[0,1]
    float3 projCoords = shadowPosH.xyz;

    // Depth in NDC space of current shading point
    float currentDepth = projCoords.z;
    // get closest depth from depth map
    float closestDepth = gShadowMap.Sample(gsamLinearWrap, projCoords.xy);

    // calculate bias

    float w_light = LightWidth;
    
    int sampleCount = 8;
    float blockerSum = 0;
    int blockerCount = 0;
    
    uint width, height, numMips;
    gShadowMap.GetDimensions(0, width, height, numMips);
    float texelSize = 1.0f / (float) width;

    float search_range = w_light * (currentDepth - 0.05) / currentDepth;
    
    if (search_range <= 0)
    {
        return 0;
    }
    int range = int(search_range);
    int window = 3;
    for (int i = -window; i < window; ++i)
    {
        for (int j = -window; j < window; ++j)
        {
            float2 shift = float2(i * 1.0 * range / window, j * 1.0 * range / window);
            
            //sampleDepth = shadow map value at location (i , j) in the search region
            float sampleDepth = gShadowMap.Sample(gsamLinearWrap, projCoords.xy + shift * texelSize).r;
            
            if (sampleDepth < currentDepth)
            {
                blockerSum += sampleDepth;
                blockerCount++;
            }
        }
    }
    float avg_depth;
    if (blockerCount > 0)
    {
        avg_depth = blockerSum / blockerCount;
    }
    else
    {
        avg_depth = 0; //--> not in shadow~~~~
    }
    
    if (avg_depth == 0)
    {
        return 0.0f;
    }
    
    float w_penumbra = w_penumbra1(currentDepth, avg_depth, w_light);
    
    //if penumbra is zero, it has solid shadow - dark
    if (w_penumbra == 0)
    {
        return 0;
    }
    
    float shadow = 0.0;
    int count = 0;
    
    //pick a searching range based on calculated penumbra width.
    int range1 = int(w_penumbra / 0.09);
    
    //manually set a range if it is too large!
    range1 = range1 > 5 ? 5 : range1;
    //range1 = range1 < 1 ? 2 : range1;
    
    //return w_penumbra;
    
    [loop]
    for (int x = -range1; x <= range1; ++x)
    {
        count++;
        [loop]
        for (int y = -range1; y <= range1; ++y)
        {
            float pcfDepth = gShadowMap.Sample(gsamLinearWrap, projCoords.xy + float2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1 : 0;
        }
    }
    //count = count > 0 ? count : 1;
    shadow /= (count * count);
    
    // // keep the shadow at 0.0 when outside the far_plane region of the light's frustum.
    // if (projCoords.z > 1.0)
    //     shadow = 0.0;
        
        
    return shadow;
}

float4 main(VertexOut pin) : SV_Target
{
    float3 albedo = Albedo;

    float3 N;
    
    N = normalize(pin.Normal);

    //return float4(N,1);
    // Outgoing light direction (vector from world-space fragment position to the "eye").
    float3 Lo = normalize(gCameraPos - pin.PosW);

    
    // Angle between surface normal and outgoing light direction.
    float cosLo = max(0.0, dot(N, Lo));

    float bias = max(0.05 * (1.0 - dot(N, gSunPosition)), 0.001);
    

    // Direct lighting calculation for analytical lights.
    float3 directLighting = 0.0;
    {
        float3 Li = normalize(gSunPosition - pin.PosW);
        float3 radiance = float3(1.0f,1.0f,1.0f);
        

        // Calculate angles between surface normal and various light vectors.
        float cosLi = max(0.0, dot(N, Li));


		// Lambert diffuse BRDF.
		// We don't scale by 1/PI for lighting & material units to be more convenient.
		// See: https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
        float3 diffuseBRDF = albedo;


		// Total contribution for this light.
        directLighting += diffuseBRDF  * radiance * cosLi;    

    }
    // Only the first light casts a shadow.
    float shadowFactor = 0;

    if(UseBasicShadow) 
        shadowFactor = Basic_ShadowCalculation(pin.ShadowPosH);
    else if(UsePCFShadow)
        shadowFactor = PCF_ShadowCalculation(pin.ShadowPosH);
    else if(UsePCSSShadow)
        shadowFactor = PCSS_ShadowCalculation(pin.ShadowPosH, bias);
    else
        shadowFactor = 0;
    
    float3 color =  directLighting * (1 - shadowFactor);

  
    return float4(color, 1.0);
}
