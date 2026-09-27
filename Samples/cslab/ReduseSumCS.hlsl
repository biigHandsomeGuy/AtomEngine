StructuredBuffer<float> Input : register(t0);
RWStructuredBuffer<float> Output : register(u0);

groupshared float gData[256];


[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
    gData[GTid.x] = Input[DTid.x];
    
    GroupMemoryBarrierWithGroupSync();
    
    for (int stride = 128; stride > 0; stride >>= 1)
    {
        if (GTid.x < stride)
        {
            gData[GTid.x] += gData[GTid.x + stride];
        }
        
        GroupMemoryBarrierWithGroupSync();
    };
    
    if (GTid.x == 0)
    {
        Output[Gid.x] = gData[0];
    };
    
}