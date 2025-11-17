#pragma once
 
#include "ConstantBuffers.h"
#include "Camera.h"
class GraphicsContext;
namespace SSAO
{
    void Initialize();

    void Render(GraphicsContext& GfxContext, const Math::Camera& camera);

    void Shutdown();
 };
 
