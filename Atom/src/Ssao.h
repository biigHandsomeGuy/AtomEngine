#pragma once
 
#include "d3dUtil.h"
#include "ConstantBuffers.h"
class Camera;
class GraphicsContext;
namespace SSAO
{
    void Initialize();

    void Render(GraphicsContext& GfxContext, const Camera& camera);

    void Shutdown();
 };
 
