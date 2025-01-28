#include "pch.h"
//#include "demo.h"
//#include "GraphicsCore.h"
//
//
//using namespace Graphics;
//using namespace Microsoft::WRL;
//float GetRandomFloat(float min, float max);
//Application* CreateApplication(HINSTANCE hInstance)
//{
//	return new Demo(hInstance);
//}
//
//
//Demo::Demo(HINSTANCE hInstance)
//	: Application(hInstance)
//{
//
//}
//
//Demo::~Demo()
//{
//	if (m_Device != nullptr)
//		FlushCommandQueue();
//
//}
//
//bool Demo::Initialize()
//{
//	if (!Application::Initialize())
//		return false;
//
//	// Reset the command list to prep for initialization commands.
//	ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator.Get(), nullptr));
//
//	m_Camera.SetPosition(0.0f, 0.0f, -2.0f);
//
//	// BuildRootSignature
//	{
//		// Root parameter can be a table, root descriptor or root constants.
//		CD3DX12_ROOT_PARAMETER slotRootParameter[kGraphicsRootParametersCount];
//
//		slotRootParameter[kCbv].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
//		
//
//
//		// A root signature is an array of root parameters.
//		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(kGraphicsRootParametersCount, slotRootParameter,
//			0, nullptr,
//			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
//
//		// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
//		ComPtr<ID3DBlob> serializedRootSig = nullptr;
//		ComPtr<ID3DBlob> errorBlob = nullptr;
//		HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
//			serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
//
//		if (errorBlob != nullptr)
//		{
//			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
//		}
//		ThrowIfFailed(hr);
//
//		ThrowIfFailed(m_Device->CreateRootSignature(
//			0,
//			serializedRootSig->GetBufferPointer(),
//			serializedRootSig->GetBufferSize(),
//			IID_PPV_ARGS(m_RootSignature.GetAddressOf())));
//	}
//
//
//	//BuildDescriptorHeaps();
//	{
//
//		//
//		// Create the SRV heap.
//		//
//		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
//		srvHeapDesc.NumDescriptors = 64;
//		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
//		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
//		ThrowIfFailed(m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvDescriptorHeap)));
//
//		// Fill out the heap with actual descriptors.
//		//
//		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
//
//	}
//
//	// BuildPSOs();
//	{
//		ComPtr<ID3DBlob> vertexShader;
//		ComPtr<ID3DBlob> pixelShader;
//		ComPtr<ID3DBlob> error;
//
//#if defined(_DEBUG)
//		// Enable better shader debugging with the graphics debugging tools.
//		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
//#else
//		UINT compileFlags = 0;
//#endif
//
//		ThrowIfFailed(D3DCompileFromFile(L"D:/Atom/Atom/Assets/Shaders/test.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error));
//		ThrowIfFailed(D3DCompileFromFile(L"D:/Atom/Atom/Assets/Shaders/test.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error));
//
//		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
//		{
//			{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
//
//		};
//
//		D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc = {};
//		basePsoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
//		basePsoDesc.pRootSignature = m_RootSignature.Get();
//		basePsoDesc.VS =
//		{
//			vertexShader->GetBufferPointer(),
//			vertexShader->GetBufferSize()
//		};
//		basePsoDesc.PS =
//		{
//			pixelShader->GetBufferPointer(),
//			pixelShader->GetBufferSize()
//		};
//		basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
//		basePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
//		basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
//		basePsoDesc.BlendState.RenderTarget[0].BlendEnable = false;
//		basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
//		basePsoDesc.SampleMask = UINT_MAX;
//		basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
//		basePsoDesc.NumRenderTargets = 1;
//		basePsoDesc.RTVFormats[0] = mBackBufferFormat;
//		basePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
//		basePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
//		basePsoDesc.DSVFormat = mDepthStencilFormat;
//
//
//		basePsoDesc.DepthStencilState.DepthEnable = true;
//		basePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
//		basePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
//		ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&basePsoDesc, IID_PPV_ARGS(&m_PSOs["opaque"])));
//	}
//	// CreateVertexBuffer();
//	ComPtr<ID3D12Resource> triangleVBUpload;
//	ComPtr<ID3D12Resource> rectangleVBUpload;
//	{
//		// Define the geometry for a triangle.
//
//		Vertex triangleVertices[3] =
//		{
//			{ { 0.0f, 0.05f, 1 ,1} },
//			{ { 0.05f, -0.05f, 1,1} },
//			{ { -0.05f, -0.05f, 1 ,1} }
//		};
//
//		Vertex rectangleVertices[4] =
//		{
//			{ { 0.05f, 0.05f, 1 ,1} },
//			{ { 0.05f, -0.05f, 1,1} },
//			{ { -0.05f, 0.05f, 1 ,1} },
//			{ { -0.05f, -0.05f, 1 ,1} },
//		};
//		const UINT triangleVBSize = sizeof(triangleVertices);
//		const UINT rectangleVBSize = sizeof(rectangleVertices);
//
//		ThrowIfFailed(m_Device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(triangleVBSize),
//			D3D12_RESOURCE_STATE_COPY_DEST,
//			nullptr,
//			IID_PPV_ARGS(&m_TriangleVertexBuffer)));
//
//		ThrowIfFailed(m_Device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(rectangleVBSize),
//			D3D12_RESOURCE_STATE_COPY_DEST,
//			nullptr,
//			IID_PPV_ARGS(&m_RectangleVertexBuffer)));
//
//		ThrowIfFailed(m_Device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(triangleVBSize),
//			D3D12_RESOURCE_STATE_GENERIC_READ,
//			nullptr,
//			IID_PPV_ARGS(&triangleVBUpload)));
//		
//		ThrowIfFailed(m_Device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(rectangleVBSize),
//			D3D12_RESOURCE_STATE_GENERIC_READ,
//			nullptr,
//			IID_PPV_ARGS(&rectangleVBUpload)));
//
//
//		// Copy data to the intermediate upload heap and then schedule a copy
//		// from the upload heap to the vertex buffer.
//		D3D12_SUBRESOURCE_DATA triangleVertexData = {};
//		triangleVertexData.pData = triangleVertices;
//		triangleVertexData.RowPitch = triangleVBSize;
//		triangleVertexData.SlicePitch = triangleVertexData.RowPitch;
//
//		D3D12_SUBRESOURCE_DATA rectangleVertexData = {};
//		rectangleVertexData.pData = rectangleVertices;
//		rectangleVertexData.RowPitch = rectangleVBSize;
//		rectangleVertexData.SlicePitch = rectangleVertexData.RowPitch;
//
//
//		UpdateSubresources<1>(m_CommandList.Get(), m_TriangleVertexBuffer.Get(), triangleVBUpload.Get(), 0, 0, 1, &triangleVertexData);
//		m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_TriangleVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
//
//		UpdateSubresources<1>(m_CommandList.Get(), m_RectangleVertexBuffer.Get(), rectangleVBUpload.Get(), 0, 0, 1, &rectangleVertexData);
//		m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_RectangleVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
//
//
//		// Initialize the vertex buffer view.
//		m_TriangleVertexBufferView.BufferLocation = m_TriangleVertexBuffer->GetGPUVirtualAddress();
//		m_TriangleVertexBufferView.StrideInBytes = sizeof(Vertex);
//		m_TriangleVertexBufferView.SizeInBytes = sizeof(triangleVertices);
//
//		m_RectangleVertexBufferView.BufferLocation = m_RectangleVertexBuffer->GetGPUVirtualAddress();
//		m_RectangleVertexBufferView.StrideInBytes = sizeof(Vertex);
//		m_RectangleVertexBufferView.SizeInBytes = sizeof(triangleVertices);
//	}
//	// CreateConstantBuffer();
//	{
//		ThrowIfFailed(m_Device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(SceneConstantBuffer)*2),
//			D3D12_RESOURCE_STATE_GENERIC_READ,
//			nullptr,
//			IID_PPV_ARGS(&m_ConstantBuffer)));
//
//
//		// Initialize the const buffers
//		m_ConstantBufferData.resize(2);
//		XMMATRIX vp = m_Camera.GetView() * m_Camera.GetProj();
//		for (int n = 0; n < 2; n++)
//		{
//			m_ConstantBufferData[n].velocity = XMFLOAT4(GetRandomFloat(0.01f, 0.02f), 0.0f, 0.0f, 0.0f);
//			m_ConstantBufferData[n].offset = XMFLOAT4(GetRandomFloat(-5.0f, -1.5f), GetRandomFloat(-1.0f, 1.0f), GetRandomFloat(0.0f, 2.0f), 0.0f);
//			m_ConstantBufferData[n].color = XMFLOAT4(GetRandomFloat(0.5f, 1.0f), GetRandomFloat(0.5f, 1.0f), GetRandomFloat(0.5f, 1.0f), 1.0f);
//			XMStoreFloat4x4(&m_ConstantBufferData[n].projection, vp);
//
//		}
//
//		
//		ThrowIfFailed(m_ConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_pCbvDataBegin)));
//		memcpy(m_pCbvDataBegin, m_ConstantBufferData.data(), sizeof(SceneConstantBuffer)* 2);
//	}
//
//	// Create the command signature used for indirect drawing.
//	{
//		D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[3] = {};
//		argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
//		argumentDescs[0].ConstantBufferView.RootParameterIndex = kCbv;
//		argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
//		argumentDescs[1].VertexBuffer.Slot = 0;
//		argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
//
//		D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
//		commandSignatureDesc.pArgumentDescs = argumentDescs;
//		commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
//		commandSignatureDesc.ByteStride = sizeof(IndirectCommand);
//
//		ThrowIfFailed(m_Device->CreateCommandSignature(&commandSignatureDesc, m_RootSignature.Get(), IID_PPV_ARGS(&m_CommandSignature)));
//	}
//	ComPtr<ID3D12Resource> commandBufferUpload;
//	std::vector<IndirectCommand> commands;
//	// Create Command Buffer
//	{
//		
//		commands.resize(2);
//
//		const UINT commandBufferSize = 2 * sizeof(IndirectCommand);
//
//		ThrowIfFailed(m_Device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(commandBufferSize),
//			D3D12_RESOURCE_STATE_COPY_DEST,
//			nullptr,
//			IID_PPV_ARGS(&m_CommandBuffer)
//		));
//
//		ThrowIfFailed(m_Device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(commandBufferSize),
//			D3D12_RESOURCE_STATE_GENERIC_READ,
//			nullptr,
//			IID_PPV_ARGS(&commandBufferUpload)));
//
//		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = m_ConstantBuffer->GetGPUVirtualAddress();
//		UINT commandIndex = 0;
//
//		for (int n = 0; n < 2; n++)
//		{
//			commands[commandIndex].cbv = gpuAddress;
//			commands[commandIndex].drawArguments.VertexCountPerInstance = 3;
//			commands[commandIndex].drawArguments.InstanceCount = 1;
//			commands[commandIndex].drawArguments.StartVertexLocation = 0;
//			commands[commandIndex].drawArguments.StartInstanceLocation = 0;
//			
//			commandIndex++;
//			gpuAddress += sizeof(SceneConstantBuffer);
//		}
//		commands[0].vbv = m_TriangleVertexBufferView;
//		commands[1].vbv = m_RectangleVertexBufferView;
//
//		D3D12_SUBRESOURCE_DATA commandData = {};
//		commandData.pData = commands.data();
//		commandData.RowPitch = commandBufferSize;
//		commandData.SlicePitch = commandData.RowPitch;
//
//		UpdateSubresources(m_CommandList.Get(), m_CommandBuffer.Get(), commandBufferUpload.Get(), 0, 0, 1, &commandData);
//		m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_CommandBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
//
//	}
//
//	// Execute the initialization commands.
//	ThrowIfFailed(m_CommandList->Close());
//	ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
//	m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
//
//	// Wait until initialization is complete.
//	FlushCommandQueue();
//
//	return true;
//}
//
//void Demo::CreateRtvAndDsvDescriptorHeaps()
//{
//	//
//	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
//	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
//	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
//	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
//	rtvHeapDesc.NodeMask = 0;
//	ThrowIfFailed(m_Device->CreateDescriptorHeap(
//		&rtvHeapDesc, IID_PPV_ARGS(m_RtvHeap.GetAddressOf())));
//
//	// Add +1 DSV for shadow map.
//	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
//	dsvHeapDesc.NumDescriptors = 1;
//	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
//	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
//	dsvHeapDesc.NodeMask = 0;
//	ThrowIfFailed(m_Device->CreateDescriptorHeap(
//		&dsvHeapDesc, IID_PPV_ARGS(m_DsvHeap.GetAddressOf())));
//
//}
//
//void Demo::OnResize()
//{
//	Application::OnResize();
//
//	m_Camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 0.01f, 200.0f);
//
//}
//
//
//
//void Demo::Update(const GameTimer& gt)
//{
//	m_Camera.Update(gt.DeltaTime());
//
//	XMMATRIX vp = m_Camera.GetView() * m_Camera.GetProj();
//	for (UINT n = 0; n < 2; n++)
//	{
//		const float offsetBounds = 2.5f;
//
//		// Animate the triangles.
//		m_ConstantBufferData[n].offset.x += m_ConstantBufferData[n].velocity.x;
//		
//		if (m_ConstantBufferData[n].offset.x > offsetBounds)
//		{
//			m_ConstantBufferData[n].velocity.x = GetRandomFloat(0.01f, 0.02f);
//			m_ConstantBufferData[n].offset.x = -offsetBounds;
//
//			
//			XMStoreFloat4x4(&m_ConstantBufferData[n].projection, vp);
//
//		}
//	}
//
//	UINT8* destination = m_pCbvDataBegin;
//	memcpy(destination, &m_ConstantBufferData[0], 2 * sizeof(SceneConstantBuffer));
//}
//
//
//
//void Demo::Draw(const GameTimer& gt)
//{
//	// Reuse the memory associated with command recording.
//	// We can only reset when the associated command lists have finished execution on the GPU.
//	ThrowIfFailed(m_CommandAllocator->Reset());
//
//	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
//	// Reusing the command list reuses memory.
//	ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator.Get(), m_PSOs["opaque"].Get()));
//
//
//	ID3D12DescriptorHeap* descriptorHeaps[] = { m_SrvDescriptorHeap.Get() };
//	m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
//	m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());
//
//
//	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
//		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
//
//	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_CommandBuffer.Get(),
//		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));
//
//	static XMFLOAT4 color = XMFLOAT4(0, 0, 0, 1);
//	// Clear the back buffer.
//	m_CommandList->ClearRenderTargetView(CurrentBackBufferView(), &color.x, 0, nullptr);
//	m_CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
//
//	m_CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());
//
//	m_CommandList->RSSetViewports(1, &m_ScreenViewport);
//	m_CommandList->RSSetScissorRects(1, &m_ScissorRect);
//	m_CommandList->SetPipelineState(m_PSOs["opaque"].Get());
//
//	//m_CommandList->SetGraphicsRootConstantBufferView(kCbv, m_ConstantBuffer->GetGPUVirtualAddress());
//	m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
//
//	
//	m_CommandList->ExecuteIndirect(
//		m_CommandSignature.Get(),
//		2,
//		m_CommandBuffer.Get(),
//		0,
//		nullptr,
//		0
//	);
//	//m_CommandList->DrawInstanced(3, 1, 0, 0);
//	
//
//	// Indicate a state transition on the resource usage.
//	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
//		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
//	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_CommandBuffer.Get(),
//		D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
//
//	// Done recording commands.
//	ThrowIfFailed(m_CommandList->Close());
//
//
//	// Add the command list to the queue for execution.
//	ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
//	m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
//
//	// Swap the back and front buffers
//	ThrowIfFailed(m_SwapChain->Present(0, 0));
//	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
//
//	// Advance the fence value to mark commands up to this fence point.
//	mCurrentFence++;
//
//	// Add an instruction to the command queue to set a new fence point. 
//	// Because we are on the GPU timeline, the new fence point won't be 
//	// set until the GPU finishes processing all the commands prior to this Signal().
//
//	const UINT64 fence = mCurrentFence;
//
//	ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), fence));
//
//	// Has the GPU finished processing the commands of the current frame resource?
//	// If not, wait until the GPU has completed commands up to this fence point.
//	if (m_Fence->GetCompletedValue() < fence)
//	{
//		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
//		ThrowIfFailed(m_Fence->SetEventOnCompletion(fence, eventHandle));
//		WaitForSingleObject(eventHandle, INFINITE);
//		CloseHandle(eventHandle);
//	}
//}
//
//// Get a random float value between min and max.
//float GetRandomFloat(float min, float max)
//{
//	float scale = static_cast<float>(rand()) / RAND_MAX;
//	float range = max - min;
//	return scale * range + min;
//}
//
