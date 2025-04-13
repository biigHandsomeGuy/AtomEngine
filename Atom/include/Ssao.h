#pragma once
 
#include "d3dUtil.h"
#include "ConstantBuffers.h"
class Camera;
class CommandContext;
namespace SSAO
{
    void Initialize();

    void Render(CommandContext& GfxContext, const Camera& camera);

    void Shutdown();
 };
 
