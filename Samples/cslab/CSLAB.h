#pragma once
#include "GameCore.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "GpuBuffer.h"
#include "ReadbackBuffer.h"
class ReduseSum : public GameCore::IGameApp
{
public:

    ReduseSum(HINSTANCE hInstance);
    ReduseSum(const ReduseSum& rhs) = delete;
    ReduseSum& operator=(const ReduseSum& rhs) = delete;
    ~ReduseSum();

    void OnResize() override;
    void Startup() override;
    void Cleanup()override {};

private:
    virtual void Update(float gt)override;
    virtual void RenderScene()override;

	RootSignature m_RootSignature;
	ComputePSO m_ComputePSO;

	StructuredBuffer m_InputBuffer;
	StructuredBuffer m_OutputBuffer;
	ReadbackBuffer m_ReadbackBuffer;
	static constexpr int m_ElementCount = 256;
	std::array<float, m_ElementCount> m_InputData;
};