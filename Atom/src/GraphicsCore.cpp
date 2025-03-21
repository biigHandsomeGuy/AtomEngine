#include "pch.h"
#include "GraphicsCore.h"
#include "Display.h"
#include "Ssao.h"
#include "CommandListManager.h"

namespace Graphics
{
    UINT RtvDescriptorSize = 0;
    UINT DsvDescriptorSize = 0;
    UINT CbvSrvUavDescriptorSize = 0;

	Microsoft::WRL::ComPtr<ID3D12Device> g_Device;
    
	ID3D12GraphicsCommandList* g_CommandList;
	ID3D12CommandAllocator* g_CommandAllocator;
	CommandListManager g_CommandManager;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_SrvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_RtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_DsvHeap;

	Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
	Microsoft::WRL::ComPtr<ID3D12DebugDevice> debugDevice;
	UINT g_CurrentFence = 0;
    void Initialize(bool RequireDXRSupport)
    {
		Microsoft::WRL::ComPtr<ID3D12Device> pDevice;
#if defined(DEBUG) || defined(_DEBUG) 
		// Enable the D3D12 debug layer.
		{		
			ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
			debugController->EnableDebugLayer();
		}
#endif
		Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
		ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));
		
		// Try to create hardware device.
		HRESULT hardwareResult = D3D12CreateDevice(
			nullptr,             // default adapter
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&pDevice));

		// Fallback to WARP device.
		if (FAILED(hardwareResult))
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter> pWarpAdapter;
			ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

			ThrowIfFailed(D3D12CreateDevice(
				pWarpAdapter.Get(),
				D3D_FEATURE_LEVEL_11_0,
				IID_PPV_ARGS(&pDevice)));
		}

		g_Device = pDevice.Detach();
		g_Device->SetName(L"g_Device");
		
		g_CommandManager.Create(g_Device.Get());

		g_CommandManager.CreateNewCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, &g_CommandList, &g_CommandAllocator);


		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
		rtvHeapDesc.NumDescriptors = 8;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		rtvHeapDesc.NodeMask = 0;
		ThrowIfFailed(g_Device->CreateDescriptorHeap(
			&rtvHeapDesc, IID_PPV_ARGS(&g_RtvHeap)));
		g_RtvHeap->SetName(L"g_RtvHeap");

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
		dsvHeapDesc.NumDescriptors = 8;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dsvHeapDesc.NodeMask = 0;
		ThrowIfFailed(g_Device->CreateDescriptorHeap(
			&dsvHeapDesc, IID_PPV_ARGS(&g_DsvHeap)));
		g_DsvHeap->SetName(L"g_DsvHeap");
		//
		// Create the SRV heap.
		//
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = 64;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(g_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&g_SrvHeap)));
		g_SrvHeap->SetName(L"g_SrvHeap");

		Graphics::RtvDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		Graphics::DsvDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		Graphics::CbvSrvUavDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);



		Display::Initialize();

		SSAO::Initialize(g_CommandList);

		uint64_t FenceValue = g_CommandManager.GetGraphicsQueue().ExecuteCommandList(g_CommandList);
		g_CommandManager.GetGraphicsQueue().WaitForFence(FenceValue);
		g_CommandManager.GetGraphicsQueue().DiscardAllocator(FenceValue, g_CommandAllocator);

    }
    void Shutdown(void)
    {
		g_CommandManager.IdleGPU();
		
		g_CommandManager.Shutdown();
		SSAO::Shutdown();
		Display::Shutdown();
    }

	
}