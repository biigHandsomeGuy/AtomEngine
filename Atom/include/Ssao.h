#pragma once
 
#include "d3dUtil.h"
#include "ConstantBuffers.h"
class Camera;

namespace SSAO
{
    void Initialize(ID3D12GraphicsCommandList* CmdList);

    void Render(const Camera& camera, ID3D12GraphicsCommandList* CmdList);

    void Shutdown();
 };
 
