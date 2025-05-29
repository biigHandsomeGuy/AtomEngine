#include "pch.h"
#include "Renderer.h"
#include "TextureManager.h"
#include "GraphicsCore.h"
#include "DescriptorHeap.h"
#include "BufferManager.h"

using namespace TextureManager;
using namespace Graphics;

namespace Renderer
{
	DescriptorHeap s_TextureHeap;
	DescriptorHandle m_CommonTextures;
	DescriptorHandle g_SSAOSrvHeap;
	DescriptorHandle g_SSAOUavHeap;
	DescriptorHandle g_PostprocessHeap;
	DescriptorHandle g_NullDescriptor;


	void Initialize(void)
	{
		TextureManager::Initialize(L"");

		s_TextureHeap.Create(L"Scene Texture Descriptors", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128);

		m_CommonTextures = s_TextureHeap.Alloc(8);
		g_PostprocessHeap = s_TextureHeap.Alloc();

		g_NullDescriptor = s_TextureHeap.Alloc();
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.Texture2D.MipLevels = -1;
		desc.Texture2D.MostDetailedMip = 0;
		desc.Texture2D.ResourceMinLODClamp = 0;
		g_Device->CreateShaderResourceView(nullptr, &desc, g_NullDescriptor);

		uint32_t DestCount = 8;
		uint32_t SourceCounts[] = { 1, 1, 1, 1, 1, 1, 1, 1};


		D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
		{
			g_EnvirMap.GetSRV(),
			g_RadianceMap.GetSRV(),
			g_IrradianceMap.GetSRV(),
			g_SSAOFullScreen.GetSRV(),
			g_ShadowBuffer.GetDepthSRV(),
			g_LUT.GetSRV(),
			g_Emu.GetSRV(),
			g_Eavg.GetSRV(),
		};

		g_Device->CopyDescriptors(1, &m_CommonTextures, &DestCount, DestCount, SourceTextures, SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		
		{
			g_SSAOSrvHeap = Renderer::s_TextureHeap.Alloc(5);
			g_SSAOUavHeap = Renderer::s_TextureHeap.Alloc();

			uint32_t DestCount = 5;
			uint32_t SourceCounts[] = { 1, 1, 1, 1, 1 };

			D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
			{
				g_SceneNormalBuffer.GetSRV(),
				g_SceneDepthBuffer.GetDepthSRV(),
				g_RandomVectorBuffer.GetSRV(),
				g_SSAOUnBlur.GetSRV(),
				g_SSAOFullScreen.GetSRV(),
			};

			g_Device->CopyDescriptors(1, &g_SSAOSrvHeap, &DestCount, DestCount, SourceTextures,
				SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_Device->CopyDescriptorsSimple(1, g_SSAOUavHeap, g_SSAOFullScreen.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

	}
	void UpdateGlobalDescriptors(void)
	{
		uint32_t DestCount = 2;
		uint32_t SourceCounts[] = { 1, 1 };


		D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
		{
			g_SSAOFullScreen.GetSRV(),
			g_ShadowBuffer.GetDepthSRV(),
		};

		DescriptorHandle dest = m_CommonTextures + 3 * s_TextureHeap.GetDescriptorSize();

		g_Device->CopyDescriptors(1, &dest, &DestCount, DestCount, SourceTextures, SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	}
}