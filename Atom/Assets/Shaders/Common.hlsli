#ifndef COMMON_HLSLI
#define COMMON_HLSLI


SamplerState gsamLinearWrap       : register(s0);
SamplerState gsamLinearClamp      : register(s1);
SamplerState gsamAnisotropicWrap  : register(s2);
SamplerState gsamAnisotropicClamp : register(s3);
SamplerComparisonState gsamShadow : register(s4);

#endif // COMMON_HLSLI