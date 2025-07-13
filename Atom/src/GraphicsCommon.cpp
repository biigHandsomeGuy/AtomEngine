#include "pch.h"
#include "GraphicsCommon.h"
#include "SamplerManager.h"

namespace Graphics
{
	SamplerDesc SamplerPointWrapDesc;
	SamplerDesc SamplerPointClampDesc;
	SamplerDesc SamplerLinearWrapDesc;
	SamplerDesc SamplerLinearClampDesc;
	SamplerDesc SamplerAnisotropicWrapDesc;
	SamplerDesc SamplerAnisotropicClampDesc;
	SamplerDesc SamplerShadowDesc;


	// UNDONE:(Ã»×ù!)

	
	D3D12_CPU_DESCRIPTOR_HANDLE GetDefaultTexture(eDefaultTexture texID)
	{
		return D3D12_CPU_DESCRIPTOR_HANDLE();
	}

	
}

void Graphics::InitializeCommonState()
{
	SamplerPointWrapDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	SamplerPointWrapDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	SamplerPointClampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	SamplerPointClampDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	SamplerLinearWrapDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	SamplerLinearWrapDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	SamplerLinearClampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	SamplerLinearClampDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	SamplerAnisotropicWrapDesc.Filter = D3D12_FILTER_ANISOTROPIC;
	SamplerAnisotropicWrapDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	SamplerAnisotropicClampDesc.Filter = D3D12_FILTER_ANISOTROPIC;
	SamplerAnisotropicClampDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	SamplerShadowDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	SamplerShadowDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	SamplerShadowDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
}

