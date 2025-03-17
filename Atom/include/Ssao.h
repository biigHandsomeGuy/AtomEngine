#pragma once
 
#include "d3dUtil.h"
#include "ConstantBuffers.h"
class Camera;

namespace SSAO
{
    void Initialize();

    void Render(const Camera& camera);

    void Shutdown();
    static const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
 };
 
