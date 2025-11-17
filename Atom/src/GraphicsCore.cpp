#include "pch.h"
#include "GraphicsCore.h"
#include "Display.h"
#include "Ssao.h"
#include "CommandListManager.h"
#include "CommandContext.h"
#include "GraphicsCommon.h"
#include "Renderer.h"

namespace Graphics
{
    UINT RtvDescriptorSize = 0;
    UINT DsvDescriptorSize = 0;
    UINT CbvSrvUavDescriptorSize = 0;

	ID3D12Device* g_Device;
    
	// ID3D12GraphicsCommandList* g_CommandList;
	// ID3D12CommandAllocator* g_CommandAllocator;
	CommandListManager g_CommandManager;
	ContextManager g_ContextManager;

	DescriptorAllocator g_DescriptorAllocator[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] =
	{
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV
	};

	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController;
	Microsoft::WRL::ComPtr<ID3D12DebugDevice> debugDevice;
	UINT g_CurrentFence = 0;
    void Initialize(bool RequireDXRSupport)
    {
		Microsoft::WRL::ComPtr<ID3D12Device> pDevice;
#if defined(DEBUG) || defined(_DEBUG) 
		// Enable the D3D12 debug layer.
		{		
			ASSERT_SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
			debugController->EnableDebugLayer();
			debugController->SetEnableGPUBasedValidation(true);
		}
#endif
		Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
		ASSERT_SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));
		
		// Try to create hardware device.
		HRESULT hardwareResult = D3D12CreateDevice(
			nullptr,             // default adapter
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&pDevice));

		// Fallback to WARP device.
		if (FAILED(hardwareResult))
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter> pWarpAdapter;
			ASSERT_SUCCEEDED(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

			ASSERT_SUCCEEDED(D3D12CreateDevice(
				pWarpAdapter.Get(),
				D3D_FEATURE_LEVEL_11_0,
				IID_PPV_ARGS(&pDevice)));
		}

		g_Device = pDevice.Detach();
		g_Device->SetName(L"g_Device");
		
		g_CommandManager.Create(g_Device);

		Graphics::InitializeCommonState();

		Graphics::RtvDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		Graphics::DsvDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		Graphics::CbvSrvUavDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		Display::Initialize();


		SSAO::Initialize();

    }
    void Shutdown(void)
    {
		g_CommandManager.IdleGPU();
		
		g_CommandManager.Shutdown();
		SSAO::Shutdown();
		Display::Shutdown();
    }

	
}