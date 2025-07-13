#pragma once

#include "SamplerManager.h"

class SamplerDesc;

namespace Graphics
{
    void InitializeCommonState();

    enum eDefaultTexture
    {
        kMagenta2D,  // Useful for indicating missing textures
        kBlackOpaque2D,
        kBlackTransparent2D,
        kWhiteOpaque2D,
        kWhiteTransparent2D,
        kDefaultNormalMap,
        kBlackCubeMap,

        kNumDefaultTextures
    };

    D3D12_CPU_DESCRIPTOR_HANDLE GetDefaultTexture(eDefaultTexture texID);

    
    // SamplerLinearWrapDesc
    extern SamplerDesc SamplerPointWrapDesc;
    extern SamplerDesc SamplerPointClampDesc;
    extern SamplerDesc SamplerLinearWrapDesc;
    extern SamplerDesc SamplerLinearClampDesc;
    extern SamplerDesc SamplerAnisotropicWrapDesc;
    extern SamplerDesc SamplerAnisotropicClampDesc;
    extern SamplerDesc SamplerShadowDesc;

}