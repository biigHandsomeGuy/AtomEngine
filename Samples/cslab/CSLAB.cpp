#include "pch.h"
#include "CSLAB.h"
#include "CommandContext.h"

namespace CS
{
#include "CompiledShaders/ReduseSumCS.h"
}

int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
	return GameCore::RunApplication(ReduseSum(hInstance), L"ReduseSum", hInstance, nCmdShow);
}
void ReduseSum::RenderScene()
{
	ComputeContext& ComputeContext = ComputeContext::Begin(L"PrecomputeCubemaps");
	ComputeContext.SetRootSignature(m_RootSignature);
	ComputeContext.SetPipelineState(m_ComputePSO);
	ComputeContext.TransitionResource(
		m_InputBuffer,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ComputeContext.TransitionResource(m_OutputBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
	ComputeContext.SetBufferSRV(0, m_InputBuffer);
	ComputeContext.SetBufferUAV(1, m_OutputBuffer);
	int groupCount =
		(m_ElementCount + 255) / 256;

	//ComputeContext.BeginResourceTransition(m_OutputBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	
	ComputeContext.Dispatch(groupCount);
	ComputeContext.CopyBuffer(
		m_ReadbackBuffer,
		m_OutputBuffer);

	ComputeContext.Finish();

	void* prt = m_ReadbackBuffer.Map();
	OutputDebugString(L"Sum: ");
	OutputDebugString(std::to_wstring(*(float*)prt).c_str());
	OutputDebugString(L"");

	m_ReadbackBuffer.Unmap();
}

ReduseSum::ReduseSum(HINSTANCE hInstance)
{

}

ReduseSum::~ReduseSum()
{

}

void ReduseSum::OnResize()
{
}

void ReduseSum::Startup()
{
	m_InputData.fill(2.50f);

	m_RootSignature.Reset(2);
	m_RootSignature[0].InitAsBufferSRV(0);
	m_RootSignature[1].InitAsBufferUAV(0);
	m_RootSignature.Finalize(L"ReduseSumRootSig");

	m_ComputePSO.SetRootSignature(m_RootSignature);
	m_ComputePSO.SetComputeShader(CS::g_pReduseSumCS, sizeof(CS::g_pReduseSumCS));
	m_ComputePSO.Finalize();

	m_InputBuffer.Create(L"InputBuffer", m_ElementCount, sizeof(float), m_InputData.data());
	m_OutputBuffer.Create(L"OutputBuffer", 1, sizeof(float), nullptr);
	m_ReadbackBuffer.Create(L"ReadBackBuffer", 1, sizeof(float));
}

void ReduseSum::Update(float gt)
{
}

