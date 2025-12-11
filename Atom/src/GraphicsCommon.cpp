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


	D3D12_RASTERIZER_DESC RasterizerDefault;
	D3D12_RASTERIZER_DESC RasterizerTwoSided;
	D3D12_RASTERIZER_DESC RasterizerShadow;

	D3D12_BLEND_DESC BlendNoColorWrite;

	D3D12_DEPTH_STENCIL_DESC DepthStateDisabled;
	D3D12_DEPTH_STENCIL_DESC DepthStateReadWrite;
	D3D12_DEPTH_STENCIL_DESC DepthStateReadOnly;
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


	RasterizerDefault = D3D12_RASTERIZER_DESC
	{
		.FillMode = D3D12_FILL_MODE_SOLID,
		.CullMode = D3D12_CULL_MODE_BACK,
		.FrontCounterClockwise = true,
		.DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
		.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
		.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
		.DepthClipEnable = TRUE,
		.MultisampleEnable = FALSE,
		.AntialiasedLineEnable = FALSE,
		.ForcedSampleCount = 0,
		.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
	};

	RasterizerTwoSided = RasterizerDefault;
	RasterizerTwoSided.CullMode = D3D12_CULL_MODE_NONE;

	RasterizerShadow = RasterizerDefault;
	RasterizerShadow.DepthBias = 100000;
	RasterizerShadow.DepthBiasClamp = 0.0f;
	RasterizerShadow.SlopeScaledDepthBias = 1.0f;

	BlendNoColorWrite = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	// CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT)

	DepthStateDisabled = D3D12_DEPTH_STENCIL_DESC
	{
		.DepthEnable = FALSE,
		.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,
		.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS,
		.StencilEnable = FALSE,
		.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
		.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
		.FrontFace =
		{
			.StencilFailOp = D3D12_STENCIL_OP_KEEP,
			.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
			.StencilPassOp = D3D12_STENCIL_OP_KEEP,
			.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
		},
		.BackFace = 
		{
			.StencilFailOp = D3D12_STENCIL_OP_KEEP,
			.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
			.StencilPassOp = D3D12_STENCIL_OP_KEEP,
			.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
		}
	};

	DepthStateReadWrite = DepthStateDisabled;
	DepthStateReadWrite.DepthEnable = TRUE;
	DepthStateReadWrite.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	DepthStateReadWrite.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	DepthStateReadOnly = DepthStateReadWrite;
	DepthStateReadOnly.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;


}

