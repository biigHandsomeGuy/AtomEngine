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
	
}

void Graphics::InitializeRenderingBuffers(uint32_t bufferWidth, uint32_t bufferHeight)
{
	g_SceneColorBuffer.Create(L"Main Color Buffer", bufferWidth, bufferHeight, 1, BackBufferFormat);
	g_SceneDepthBuffer.Create(L"Scene Depth Buffer", bufferWidth, bufferHeight, DXGI_FORMAT_D24_UNORM_S8_UINT);
	g_SceneNormalBuffer.Create(L"Normals Buffer", bufferWidth, bufferHeight, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
	g_ShadowBuffer.Create(L"Shadow Map", 2048, 2048, DXGI_FORMAT_D16_UNORM);

	g_SSAOFullScreen.Create(L"SSAO Full Res", bufferWidth/2, bufferHeight/2, 1, DXGI_FORMAT_R8_UNORM);
	g_SSAOUnBlur.Create(L"SSAO Full Res", bufferWidth/2, bufferHeight/2, 1, DXGI_FORMAT_R8_UNORM);

	g_EnvirMap.CreateArray(L"Environment Map", 512, 512, 6, 10, DXGI_FORMAT_R16G16B16A16_FLOAT);
	g_RadianceMap.CreateArray(L"Radiance Map", 256, 256, 6, 9, DXGI_FORMAT_R16G16B16A16_FLOAT);
	g_IrradianceMap.CreateArray(L"Irradiance Map", 64, 64, 6, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
	g_LUT.Create(L"Specular BRDF", 512, 512, 1, DXGI_FORMAT_R16G16_FLOAT);
	g_Emu.Create(L"emu", 512, 512, 1, DXGI_FORMAT_R32_FLOAT);
	g_Eavg.Create(L"eavg", 512, 512, 1, DXGI_FORMAT_R32_FLOAT);
}

void Graphics::ResizeDisplayDependentBuffers(uint32_t bufferWidth, uint32_t bufferHeight)
{
	g_SceneColorBuffer.Create(L"Main Color Buffer", bufferWidth, bufferHeight, 1, BackBufferFormat);
	g_SceneDepthBuffer.Create(L"Scene Depth Buffer", bufferWidth, bufferHeight, DXGI_FORMAT_D24_UNORM_S8_UINT);
	g_SceneNormalBuffer.Create(L"Normals Buffer", bufferWidth, bufferHeight, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
	g_ShadowBuffer.Create(L"Shadow Map", 2048, 2048, DXGI_FORMAT_D24_UNORM_S8_UINT);

	g_SSAOFullScreen.Create(L"SSAO Full Res", bufferWidth/2, bufferHeight/2, 1, DXGI_FORMAT_R8_UNORM);
	g_SSAOUnBlur.Create(L"SSAO Full Res", bufferWidth / 2, bufferHeight / 2, 1, DXGI_FORMAT_R8_UNORM);

}

void Graphics::DestroyRenderingBuffers()
{
	g_SceneColorBuffer.Destroy();
	g_SceneDepthBuffer.Destroy();
	g_SceneNormalBuffer.Destroy();
	g_ShadowBuffer.Destroy();
	g_SSAOFullScreen.Destroy();
	g_SSAOUnBlur.Destroy();
	g_RandomVectorBuffer.Destroy();

	g_EnvirMap.Destroy();
	g_RadianceMap.Destroy();
	g_IrradianceMap.Destroy();
	g_LUT.Destroy();
	g_Emu.Destroy();
	g_Eavg.Destroy();
}
