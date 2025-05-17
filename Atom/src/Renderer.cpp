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
	DescriptorHandle TestHandle;
	TextureRef TestRef;

	void Initialize(void)
	{
		TextureManager::Initialize(L"");

		s_TextureHeap.Create(L"Scene Texture Descriptors", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128);

		m_CommonTextures = s_TextureHeap.Alloc(7);
		UINT descriptorRangeSize = 1;
		uint32_t DestCount = 7;
		uint32_t SourceCounts[] = { 1, 1, 1, 1, 1, 1, 1};


		D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
		{
			g_RadianceMap.SrvHandle,
			g_IrradianceMap.SrvHandle,
			g_SSAOFullScreen.SrvHandle,
			g_ShadowBuffer.SrvHandle,
			g_LUT.SrvHandle,
			g_Emu.SrvHandle,
			g_Eavg.SrvHandle
		};

		g_Device->CopyDescriptors(1, &m_CommonTextures, &DestCount, DestCount, SourceTextures, SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		
		{
			g_SSAOSrvHeap = Renderer::s_TextureHeap.Alloc(5);
			g_SSAOUavHeap = Renderer::s_TextureHeap.Alloc();

			uint32_t DestCount = 5;
			uint32_t SourceCounts[] = { 1, 1, 1, 1, 1 };

			D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
			{
				g_SceneNormalBuffer.SrvHandle,
				g_SceneDepthBuffer.SrvHandle,
				g_RandomVectorBuffer.SrvHandle,
				g_SSAOUnBlur.SrvHandle,
				g_SSAOFullScreen.SrvHandle,
			};

			g_Device->CopyDescriptors(1, &g_SSAOSrvHeap, &DestCount, DestCount, SourceTextures,
				SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_Device->CopyDescriptorsSimple(1, g_SSAOUavHeap, g_SSAOFullScreen.UavHandle[0], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

	}
	void UpdateGlobalDescriptors(void)
	{

		
	}
}