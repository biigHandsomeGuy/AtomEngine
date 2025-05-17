#include "pch.h"
#include "BufferManager.h"
#include "GraphicsCore.h"
#include "Display.h"
#include "CommandListManager.h"
DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;



namespace Graphics
{
	ColorBuffer g_SceneColorBuffer;
	DepthBuffer g_SceneDepthBuffer;
	ColorBuffer g_SceneNormalBuffer;
	DepthBuffer g_ShadowBuffer;
	ColorBuffer g_SSAOFullScreen;
	ColorBuffer g_SSAOUnBlur;
	ColorBuffer g_RandomVectorBuffer;

	ColorBuffer g_EnvirMap;
	ColorBuffer g_RadianceMap;
	ColorBuffer g_IrradianceMap;
	ColorBuffer g_LUT;
	ColorBuffer g_Emu;
	ColorBuffer g_Eavg;
    void InitializeRenderingBuffers(uint32_t NativeWidth, uint32_t NativeHeight)
    {
        // g_CommandManager.CreateNewCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, &g_CommandList, &g_CommandAllocator);


        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D12_RESOURCE_DESC desc = {};

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = BackBufferFormat; // 你的RTV格式
        clearValue.Color[0] = 0.0f;  // R
        clearValue.Color[1] = 0.0f;  // G
        clearValue.Color[2] = 0.0f;  // B
        clearValue.Color[3] = 1.0f;  // A

        desc.Width = NativeWidth;
        desc.Height = NativeHeight;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = BackBufferFormat;
        desc.MipLevels = 1;
        desc.DepthOrArraySize = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;

        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            &clearValue,
            IID_PPV_ARGS(&g_SceneColorBuffer.Resource)));
        g_SceneColorBuffer.Resource->SetName(L"g_SceneColorBuffer");

        // Create rtv
        
        rtvDesc.Format = BackBufferFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;

        g_SceneColorBuffer.RtvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        g_Device->CreateRenderTargetView(
            g_SceneColorBuffer.Resource.Get(),
            &rtvDesc,
            g_SceneColorBuffer.RtvHandle
        );


        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Format = BackBufferFormat;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = -1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0;

		g_SceneColorBuffer.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		
        g_Device->CreateShaderResourceView(
            g_SceneColorBuffer.Resource.Get(),
            &srvDesc,
			g_SceneColorBuffer.SrvHandle);

        // g_SceneNormalBuffer srv + rtv
        desc.Width = NativeWidth;
        desc.Height = NativeHeight;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.MipLevels = 1;
        desc.DepthOrArraySize = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        
        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
			D3D12_RESOURCE_STATE_COMMON,
            &clearValue,
            IID_PPV_ARGS(&g_SceneNormalBuffer.Resource)));
        g_SceneNormalBuffer.Resource->SetName(L"g_SceneNormalBuffer");

        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		g_SceneNormalBuffer.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_Device->CreateShaderResourceView(g_SceneNormalBuffer.Resource.Get(), &srvDesc, g_SceneNormalBuffer.SrvHandle);

		g_SceneNormalBuffer.RtvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        g_Device->CreateRenderTargetView(g_SceneNormalBuffer.Resource.Get(), &rtvDesc, g_SceneNormalBuffer.RtvHandle);

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = DepthStencilFormat;
        depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0;

        // g_DepthStencilBuffer srv + dsv
        D3D12_RESOURCE_DESC depthStencilDesc;
        depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthStencilDesc.Alignment = 0;
        depthStencilDesc.Width = NativeWidth;
        depthStencilDesc.Height = NativeHeight;
        depthStencilDesc.DepthOrArraySize = 1;
        depthStencilDesc.MipLevels = 1;
        depthStencilDesc.Format = DepthStencilFormat;
        depthStencilDesc.SampleDesc.Count = 1;
        depthStencilDesc.SampleDesc.Quality = 0;
        depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDesc,
            D3D12_RESOURCE_STATE_COMMON,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&g_SceneDepthBuffer.Resource)));
        g_SceneDepthBuffer.Resource->SetName(L"g_SceneDepthBuffer");
		g_SceneDepthBuffer.DsvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        // Create descriptor to mip level 0 of entire resource using the format of the resource.
        g_Device->CreateDepthStencilView(g_SceneDepthBuffer.Resource.Get(), nullptr, g_SceneDepthBuffer.DsvHandle);
        srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		g_SceneDepthBuffer.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_Device->CreateShaderResourceView(g_SceneDepthBuffer.Resource.Get(), &srvDesc, g_SceneDepthBuffer.SrvHandle);


        // g_ShadowBuffer
        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDesc,
            D3D12_RESOURCE_STATE_COMMON,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&g_ShadowBuffer.Resource)));
		g_ShadowBuffer.Resource->SetName(L"g_ShadowBuffer");

        srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		g_ShadowBuffer.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_Device->CreateShaderResourceView(g_ShadowBuffer.Resource.Get(), &srvDesc, g_ShadowBuffer.SrvHandle);

		g_ShadowBuffer.DsvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        g_Device->CreateDepthStencilView(g_ShadowBuffer.Resource.Get(), nullptr, g_ShadowBuffer.DsvHandle);
        clearValue.Format = DXGI_FORMAT_R8_UNORM;

        // g_SSAOFullScreen
        desc.Width = NativeWidth/2;
        desc.Height = NativeHeight/2;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.MipLevels = 1;
        desc.DepthOrArraySize = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr ,
            IID_PPV_ARGS(&g_SSAOFullScreen.Resource)));
		g_SSAOFullScreen.Resource->SetName(L"g_SSAOFullScreen");

        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
		g_SSAOFullScreen.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_Device->CreateShaderResourceView(g_SSAOFullScreen.Resource.Get(), &srvDesc, g_SSAOFullScreen.SrvHandle);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;
        uavDesc.Format = DXGI_FORMAT_R8_UNORM;

		g_SSAOFullScreen.UavHandle[0] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_Device->CreateUnorderedAccessView(g_SSAOFullScreen.Resource.Get(), 0, &uavDesc, g_SSAOFullScreen.UavHandle[0]);

        // ssao un blur

        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.MipLevels = 1;
        desc.DepthOrArraySize = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            &clearValue,
            IID_PPV_ARGS(&g_SSAOUnBlur.Resource)));
		g_SSAOUnBlur.Resource->SetName(L"g_SSAOUnBlur");

        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
		g_SSAOUnBlur.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_Device->CreateShaderResourceView(g_SSAOUnBlur.Resource.Get(), &srvDesc, g_SSAOUnBlur.SrvHandle);

		g_SSAOUnBlur.RtvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        rtvDesc.Format = DXGI_FORMAT_R8_UNORM;
        g_Device->CreateRenderTargetView(g_SSAOUnBlur.Resource.Get(), &rtvDesc, g_SSAOUnBlur.RtvHandle);

		//g_SSAOUnBlur.UavHandle.[i] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		//g_Device->CreateUnorderedAccessView(g_SSAOUnBlur.Resource.Get(), 0, &uavDesc, g_SSAOUnBlur.UavHandle[0]);


		// Create environment Map
		{
			D3D12_RESOURCE_DESC desc = {};
			desc.Width = 512;
			desc.Height = 512;
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.MipLevels = 10;
			desc.DepthOrArraySize = 6;
			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;

			ThrowIfFailed(g_Device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&g_EnvirMap.Resource)));
			g_EnvirMap.Resource->SetName(L"g_EnvirMap");

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = -1;

			//srvDesc.TextureCube.ResourceMinLODClamp = 0;

			g_EnvirMap.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			g_Device->CreateShaderResourceView(g_EnvirMap.Resource.Get(), &srvDesc, g_EnvirMap.SrvHandle);

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			for (int i = 0; i < 10; i++)
			{
				uavDesc.Texture2DArray.ArraySize = 6;
				uavDesc.Texture2DArray.FirstArraySlice = 0;
				uavDesc.Texture2DArray.MipSlice = i;
				uavDesc.Texture2DArray.PlaneSlice = 0;
				g_EnvirMap.UavHandle[i] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

				g_Device->CreateUnorderedAccessView(g_EnvirMap.Resource.Get(), nullptr, &uavDesc, g_EnvirMap.UavHandle[i]);
			}
		}


		// Create prefiltered environment Map 
		{
			D3D12_RESOURCE_DESC desc = {};
			desc.Width = 256;
			desc.Height = 256;
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.MipLevels = 9;
			desc.DepthOrArraySize = 6;
			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;

			ThrowIfFailed(g_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&g_RadianceMap.Resource)));
			g_RadianceMap.Resource->SetName(L"g_RadianceMap");

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MipLevels = -1;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.ResourceMinLODClamp = 0;

			g_RadianceMap.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			g_Device->CreateShaderResourceView(g_RadianceMap.Resource.Get(), &srvDesc, g_RadianceMap.SrvHandle);

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			for (int i = 0; i < 9; i++)
			{
				uavDesc.Texture2DArray.ArraySize = 6;
				uavDesc.Texture2DArray.FirstArraySlice = 0;
				uavDesc.Texture2DArray.MipSlice = i;
				uavDesc.Texture2DArray.PlaneSlice = 0;
				g_RadianceMap.UavHandle[i] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

				g_Device->CreateUnorderedAccessView(g_RadianceMap.Resource.Get(), nullptr, &uavDesc, g_RadianceMap.UavHandle[i]);
			}

		}

		// Create irradiance Map 
		{
			D3D12_RESOURCE_DESC irMapDesc = {};
			irMapDesc.Width = 64;
			irMapDesc.Height = 64;
			irMapDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			irMapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			irMapDesc.MipLevels = 1;
			irMapDesc.DepthOrArraySize = 6;
			irMapDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			irMapDesc.SampleDesc.Count = 1;
			irMapDesc.SampleDesc.Quality = 0;

			ThrowIfFailed(g_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&irMapDesc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&g_IrradianceMap.Resource)));
			g_IrradianceMap.Resource->SetName(L"g_IrradianceMap");

			D3D12_SHADER_RESOURCE_VIEW_DESC IrMapSrvDesc = {};
			IrMapSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			IrMapSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			IrMapSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			IrMapSrvDesc.TextureCube.MipLevels = -1;
			IrMapSrvDesc.TextureCube.MostDetailedMip = 0;
			IrMapSrvDesc.TextureCube.ResourceMinLODClamp = 0;

			g_IrradianceMap.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			g_Device->CreateShaderResourceView(g_IrradianceMap.Resource.Get(), &IrMapSrvDesc, g_IrradianceMap.SrvHandle);

			D3D12_UNORDERED_ACCESS_VIEW_DESC IrMapUavDesc = {};
			IrMapUavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			IrMapUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			IrMapUavDesc.Texture2DArray.ArraySize = 6;
			IrMapUavDesc.Texture2DArray.FirstArraySlice = 0;
			IrMapUavDesc.Texture2DArray.MipSlice = 0;
			IrMapUavDesc.Texture2DArray.PlaneSlice = 0;
			g_IrradianceMap.UavHandle[0] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			g_Device->CreateUnorderedAccessView(g_IrradianceMap.Resource.Get(), nullptr, &IrMapUavDesc, g_IrradianceMap.UavHandle[0]);

		}

		// LUT
		{
			D3D12_RESOURCE_DESC desc = {};
			desc.Width = 512;
			desc.Height = 512;
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Format = DXGI_FORMAT_R16G16_FLOAT;
			desc.MipLevels = 1;
			desc.DepthOrArraySize = 1;
			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;

			ThrowIfFailed(g_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&g_LUT.Resource)));
			g_LUT.Resource->SetName(L"g_LUT");

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = -1;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0;

			g_LUT.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			g_Device->CreateShaderResourceView(g_LUT.Resource.Get(), &srvDesc, g_LUT.SrvHandle);


			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = desc.Format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			uavDesc.Texture2D.PlaneSlice = 0;

			g_LUT.UavHandle[0] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_Device->CreateUnorderedAccessView(g_LUT.Resource.Get(), nullptr, &uavDesc, g_LUT.UavHandle[0]);
		}

		{
			D3D12_RESOURCE_DESC desc = {};
			desc.Width = 512;
			desc.Height = 512;
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.MipLevels = 1;
			desc.DepthOrArraySize = 1;
			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;

			ThrowIfFailed(g_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&g_Emu.Resource)));
			g_Emu.Resource->SetName(L"g_Emu");

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0;

			g_Emu.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			g_Device->CreateShaderResourceView(g_Emu.Resource.Get(), &srvDesc, g_Emu.SrvHandle);


			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = desc.Format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			uavDesc.Texture2D.PlaneSlice = 0;

			g_Emu.UavHandle[0] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_Device->CreateUnorderedAccessView(g_Emu.Resource.Get(), nullptr, &uavDesc, g_Emu.UavHandle[0]);
		}

		{
			D3D12_RESOURCE_DESC desc = {};
			desc.Width = 512;
			desc.Height = 512;
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.MipLevels = 1;
			desc.DepthOrArraySize = 1;
			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;

			ThrowIfFailed(g_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(&g_Eavg.Resource)));
			g_Eavg.Resource->SetName(L"g_Eavg");

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0;

			g_Eavg.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			g_Device->CreateShaderResourceView(g_Eavg.Resource.Get(), &srvDesc, g_Eavg.SrvHandle);


			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = desc.Format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			uavDesc.Texture2D.PlaneSlice = 0;

			g_Eavg.UavHandle[0] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_Device->CreateUnorderedAccessView(g_Eavg.Resource.Get(), nullptr, &uavDesc, g_Eavg.UavHandle[0]);
		}

    }

	void ResizeDisplayDependentBuffers(uint32_t NativeWidth, uint32_t NativeHeight)
	{

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D12_RESOURCE_DESC desc = {};

		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = BackBufferFormat; // 你的RTV格式
		clearValue.Color[0] = 0.0f;  // R
		clearValue.Color[1] = 0.0f;  // G
		clearValue.Color[2] = 0.0f;  // B
		clearValue.Color[3] = 1.0f;  // A

		desc.Width = NativeWidth;
		desc.Height = NativeHeight;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Format = BackBufferFormat;
		desc.MipLevels = 1;
		desc.DepthOrArraySize = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		ThrowIfFailed(g_Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			&clearValue,
			IID_PPV_ARGS(&g_SceneColorBuffer.Resource)));
		g_SceneColorBuffer.Resource->SetName(L"g_SceneColorBuffer");

		// Create rtv

		rtvDesc.Format = BackBufferFormat;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		rtvDesc.Texture2D.PlaneSlice = 0;

		g_SceneColorBuffer.RtvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		g_Device->CreateRenderTargetView(
			g_SceneColorBuffer.Resource.Get(),
			&rtvDesc,
			g_SceneColorBuffer.RtvHandle
		);


		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Format = BackBufferFormat;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = -1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0;

		g_SceneColorBuffer.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		g_Device->CreateShaderResourceView(
			g_SceneColorBuffer.Resource.Get(),
			&srvDesc,
			g_SceneColorBuffer.SrvHandle);


		// g_SceneNormalBuffer srv + rtv
		desc.Width = NativeWidth;
		desc.Height = NativeHeight;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.MipLevels = 1;
		desc.DepthOrArraySize = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		ThrowIfFailed(g_Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_COMMON,
			&clearValue,
			IID_PPV_ARGS(&g_SceneNormalBuffer.Resource)));
		g_SceneNormalBuffer.Resource->SetName(L"g_SceneNormalBuffer");

		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		g_SceneNormalBuffer.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		g_Device->CreateShaderResourceView(g_SceneNormalBuffer.Resource.Get(), &srvDesc, g_SceneNormalBuffer.SrvHandle);

		g_SceneNormalBuffer.RtvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		g_Device->CreateRenderTargetView(g_SceneNormalBuffer.Resource.Get(), &rtvDesc, g_SceneNormalBuffer.RtvHandle);


		D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
		depthOptimizedClearValue.Format = DepthStencilFormat;
		depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
		depthOptimizedClearValue.DepthStencil.Stencil = 0;

		// g_DepthStencilBuffer srv + dsv
		D3D12_RESOURCE_DESC depthStencilDesc;
		depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilDesc.Alignment = 0;
		depthStencilDesc.Width = NativeWidth;
		depthStencilDesc.Height = NativeHeight;
		depthStencilDesc.DepthOrArraySize = 1;
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.Format = DepthStencilFormat;
		depthStencilDesc.SampleDesc.Count = 1;
		depthStencilDesc.SampleDesc.Quality = 0;
		depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		ThrowIfFailed(g_Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_COMMON,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&g_SceneDepthBuffer.Resource)));
		g_SceneDepthBuffer.Resource->SetName(L"g_SceneDepthBuffer");
		g_SceneDepthBuffer.DsvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		// Create descriptor to mip level 0 of entire resource using the format of the resource.
		g_Device->CreateDepthStencilView(g_SceneDepthBuffer.Resource.Get(), nullptr, g_SceneDepthBuffer.DsvHandle);
		srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		g_SceneDepthBuffer.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		g_Device->CreateShaderResourceView(g_SceneDepthBuffer.Resource.Get(), &srvDesc, g_SceneDepthBuffer.SrvHandle);
		
		// g_ShadowBuffer
		ThrowIfFailed(g_Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_COMMON,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&g_ShadowBuffer.Resource)));
		g_ShadowBuffer.Resource->SetName(L"g_ShadowBuffer");

		srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		g_ShadowBuffer.SrvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		g_Device->CreateShaderResourceView(g_ShadowBuffer.Resource.Get(), &srvDesc, g_ShadowBuffer.SrvHandle);

		g_ShadowBuffer.DsvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		g_Device->CreateDepthStencilView(g_ShadowBuffer.Resource.Get(), nullptr, g_ShadowBuffer.DsvHandle);
		clearValue.Format = DXGI_FORMAT_R8_UNORM;

	}

    void DestroyRenderingBuffers()
    {


    }

}