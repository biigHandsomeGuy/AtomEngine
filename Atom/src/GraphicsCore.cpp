#include "pch.h"
#include "GraphicsCore.h"
#include "Display.h"
#include "Ssao.h"

namespace Graphics
{
    UINT RtvDescriptorSize = 0;
    UINT DsvDescriptorSize = 0;
    UINT CbvSrvUavDescriptorSize = 0;

	Microsoft::WRL::ComPtr<ID3D12Device> g_Device;
    // CommandListManager g_CommandManager;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> g_CommandList;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> g_CommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> g_CommandQueue;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_SrvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_RtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_DsvHeap;
	Microsoft::WRL::ComPtr<ID3D12Fence> g_Fence;

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
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // 直接命令队列
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask = 0;

		ThrowIfFailed(g_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_CommandQueue)));
		g_CommandQueue->SetName(L"g_CommandQueue");
		ThrowIfFailed(g_Device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_CommandAllocator)));
		g_CommandAllocator->SetName(L"g_CommandAllocator");
		ThrowIfFailed(g_Device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			g_CommandAllocator.Get(),
			nullptr, IID_PPV_ARGS(&g_CommandList)));
		g_CommandList->SetName(L"g_CommandList");
		ThrowIfFailed(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_Fence)));
		g_Fence->SetName(L"g_Fence");
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

		SSAO::Initialize();
		FlushCommandQueue();
    }
    void Shutdown(void)
    {
		
		FlushCommandQueue();
		g_Device.Reset();
		g_CommandList.Reset();
		g_CommandAllocator.Reset();
		g_CommandQueue.Reset();
		g_SrvHeap.Reset();
		g_RtvHeap.Reset();
		g_DsvHeap.Reset();
		g_Fence.Reset();

		debugController.Reset();
		debugDevice.Reset();
		SSAO::Shutdown();
		Display::Shutdown();
		// if (SUCCEEDED(g_Device.As(&debugDevice)))
		// {
		// 	debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL);
		// }
    }

	void FlushCommandQueue()
	{
		// Advance the fence value to mark commands up to this fence point.
		g_CurrentFence++;

		// Add an instruction to the command queue to set a new fence point.  Because we 
		// are on the GPU timeline, the new fence point won't be set until the GPU finishes
		// processing all the commands prior to this Signal().
		ThrowIfFailed(g_CommandQueue->Signal(g_Fence.Get(), g_CurrentFence));

		// Wait until the GPU has completed commands up to this fence point.
		if (g_Fence->GetCompletedValue() < g_CurrentFence)
		{
			HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

			// Fire event when GPU hits current fence.  
			ThrowIfFailed(g_Fence->SetEventOnCompletion(g_CurrentFence, eventHandle));

			// Wait until the GPU hits current fence event is fired.
			WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
		}
	}
}