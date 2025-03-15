#pragma once
 
#include "d3dUtil.h"
#include "ConstantBuffers.h"


namespace SSAO
{
    void Initialize();

    void Render(SsaoConstants& ssaoConstants);

    void Shutdown();
    static const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
 };
 
