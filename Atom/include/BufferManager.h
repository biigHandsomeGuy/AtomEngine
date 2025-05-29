#pragma once

#include "ColorBuffer.h"
#include "DepthBuffer.h"


namespace Graphics
{
    extern ColorBuffer g_SceneColorBuffer;
    extern DepthBuffer g_SceneDepthBuffer;
    extern ColorBuffer g_SceneNormalBuffer;
    extern DepthBuffer g_ShadowBuffer;
    extern ColorBuffer g_SSAOFullScreen;
    extern ColorBuffer g_SSAOUnBlur;
    extern ColorBuffer g_RandomVectorBuffer;

    extern ColorBuffer g_RadianceMap; // cube map with different roughness mip maps
    extern ColorBuffer g_EnvirMap; // cube map with different mip maps
    extern ColorBuffer g_IrradianceMap;
    extern ColorBuffer g_LUT;
    extern ColorBuffer g_Emu;
    extern ColorBuffer g_Eavg;

    void InitializeRenderingBuffers(uint32_t NativeWidth, uint32_t NativeHeight);
    void ResizeDisplayDependentBuffers(uint32_t NativeWidth, uint32_t NativeHeight);
    void DestroyRenderingBuffers();
}