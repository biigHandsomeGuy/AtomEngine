#pragma once

namespace Graphics
{
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
    extern const CD3DX12_STATIC_SAMPLER_DESC SamplerLinearWrapDesc;
    extern const CD3DX12_STATIC_SAMPLER_DESC SamplerLinearClampDesc;
    extern const CD3DX12_STATIC_SAMPLER_DESC SamplerAnisotropicWrapDesc;
    extern const CD3DX12_STATIC_SAMPLER_DESC SamplerAnisotropicClampDesc;
    extern const CD3DX12_STATIC_SAMPLER_DESC SamplerShadowDesc;

}