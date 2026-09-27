#include "pch.h"
#include "PbrResources.h"
#include "ModelAssetManager.h"
#include "GraphicsCore.h"
#include "CommandContext.h"
#include "PathUtils.h"
#include <algorithm>
#include <stdexcept>
namespace CS
{
#include "../CompiledShaders/SpecularBRDFCS.h"
#include "../CompiledShaders/IrradianceMapCS.h"
#include "../CompiledShaders/SpecularMapCS.h"
#include "../CompiledShaders/EquirectToCubeCS.h"
#include "../CompiledShaders/GenerateMipMapCS.h"
#include "../CompiledShaders/Emu.h"
#include "../CompiledShaders/Eavg.h"
#include "../CompiledShaders/PreIntegralDiffuseSSSCS.h"
#include "../CompiledShaders/PreIntegralSpecularSSSCS.h"

}
using namespace Graphics;
using namespace CS;

void PbrResources::Initialize(const std::filesystem::path& root, ModelAssetManager& assets,
    Renderer::PipelineFormats formats)
{
    if (m_Initialized) throw std::logic_error("PBR resources are already initialized.");
    Pipelines.Initialize(formats);
    Environment.CreateArray(L"PBR environment", 2048, 2048, 6, 10, DXGI_FORMAT_R16G16B16A16_FLOAT);
    Radiance.CreateArray(L"PBR radiance", 1024, 1024, 6, 9, DXGI_FORMAT_R16G16B16A16_FLOAT);
    Irradiance.CreateArray(L"PBR irradiance", 64, 64, 6, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    BrdfLut.Create(L"PBR BRDF", 512, 512, 1, DXGI_FORMAT_R16G16_FLOAT);
    Emu.Create(L"PBR emu", 512, 512, 1, DXGI_FORMAT_R32_FLOAT);
    Eavg.Create(L"PBR eavg", 512, 512, 1, DXGI_FORMAT_R32_FLOAT);
    SssDiffuse.Create(L"PBR diffuse SSS", 512, 512, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
    SssSpecular.Create(L"PBR specular SSS", 512, 512, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
    const auto source = g_TextureManager.LoadHDR((root / "Assets/Textures/EnvirMap/sun.hdr").wstring());
    Precompute(source);
    std::string error;
    auto skybox = assets.LoadBlocking(PathToUtf8(root / "Assets/Models/cube.glb"), &error);
    if (!skybox.GPU) throw std::runtime_error("Could not load the PBR skybox: " + error);
    Skybox = ModelUtility::CreateModel(std::move(skybox));
    ++m_PrecomputeCount;
    m_Initialized = true;
}

void PbrResources::Shutdown()
{
    Skybox = Model{};
    Environment.Destroy();
    Radiance.Destroy();
    Irradiance.Destroy();
    BrdfLut.Destroy();
    Emu.Destroy();
    Eavg.Destroy();
    SssDiffuse.Destroy();
    SssSpecular.Destroy();
    m_Initialized = false;
}
void PbrResources::Precompute(TextureID source)
{
	std::unordered_map<std::string, ComputePSO> pipelines;
    ComputeContext& ComputeContext = ComputeContext::Begin(L"PrecomputeCubemaps");
    const auto CeilDiv = [](UINT value, UINT divisor) -> UINT
    {
        return (value + divisor - 1) / divisor;
    };
    const auto MipSize = [](UINT baseSize, UINT level) -> UINT
    {
        return std::max(1u, baseSize >> level);
    };

	__declspec(align(256)) struct ShaderParams
	{
		DWORD param0 = 0;
		DWORD param1 = 0;
	};
	ShaderParams shaderParams;
	pipelines["EquirectToCube"].SetRootSignature(Pipelines.ComputeRoot);
	pipelines["EquirectToCube"].SetComputeShader(g_pEquirectToCubeCS, sizeof(g_pEquirectToCubeCS));
	pipelines["EquirectToCube"].Finalize();

	pipelines["GenerateMipMap"].SetRootSignature(Pipelines.ComputeRoot);
	pipelines["GenerateMipMap"].SetComputeShader(g_pGenerateMipMapCS, sizeof(g_pGenerateMipMapCS));
	pipelines["GenerateMipMap"].Finalize();

	pipelines["PrefilterSpecularMap"].SetRootSignature(Pipelines.ComputeRoot);
	pipelines["PrefilterSpecularMap"].SetComputeShader(g_pSpecularMapCS, sizeof(g_pSpecularMapCS));
	pipelines["PrefilterSpecularMap"].Finalize();

	pipelines["PrecomputeIrradianceMap"].SetRootSignature(Pipelines.ComputeRoot);
	pipelines["PrecomputeIrradianceMap"].SetComputeShader(g_pIrradianceMapCS, sizeof(g_pIrradianceMapCS));
	pipelines["PrecomputeIrradianceMap"].Finalize();

	pipelines["PrecomputeBRDF"].SetRootSignature(Pipelines.ComputeRoot);
	pipelines["PrecomputeBRDF"].SetComputeShader(g_pSpecularBRDFCS, sizeof(g_pSpecularBRDFCS));
	pipelines["PrecomputeBRDF"].Finalize();

	pipelines["emu"].SetRootSignature(Pipelines.ComputeRoot);
	pipelines["emu"].SetComputeShader(g_pEmu, sizeof(g_pEmu));
	pipelines["emu"].Finalize();

	pipelines["eavg"].SetRootSignature(Pipelines.ComputeRoot);
	pipelines["eavg"].SetComputeShader(g_pEavg, sizeof(g_pEavg));
	pipelines["eavg"].Finalize();

	pipelines["PreintegralDiffuseSSS"].SetRootSignature(Pipelines.ComputeRoot);
	pipelines["PreintegralDiffuseSSS"].SetComputeShader(g_pPreIntegralDiffuseSSSCS, sizeof(g_pPreIntegralDiffuseSSSCS));
	pipelines["PreintegralDiffuseSSS"].Finalize();

	pipelines["PreintegralSpecularSSS"].SetRootSignature(Pipelines.ComputeRoot);
	pipelines["PreintegralSpecularSSS"].SetComputeShader(g_pPreIntegralSpecularSSSCS, sizeof(g_pPreIntegralSpecularSSSCS));
	pipelines["PreintegralSpecularSSS"].Finalize();

	{

		ComputeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Graphics::g_BindlessHeap.GetHeapPointer());

		ComputeContext.SetRootSignature(Pipelines.ComputeRoot);
		ComputeContext.SetPipelineState(pipelines["EquirectToCube"]);

		ComputeContext.TransitionResource(Environment, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		ComputeContext.SetDynamicDescriptor(0, 0, g_TextureManager.Get(source).GetSRV());
		ComputeContext.SetDynamicDescriptor(1, 0, Environment.GetUAVArray()[0]);
		ComputeContext.SetDynamicConstantBufferView(2, sizeof(ShaderParams), &shaderParams);
		ComputeContext.Dispatch(CeilDiv(Environment.GetWidth(), 32), CeilDiv(Environment.GetHeight(), 32), 6);


		//// =======================  Generic MipMap
		ComputeContext.SetDynamicDescriptor(0, 0, Environment.GetSRV());
		ComputeContext.SetPipelineState(pipelines["GenerateMipMap"]);

        const UINT envMipLevels = Environment.GetResource()->GetDesc().MipLevels;
		for (UINT level = 1; level < envMipLevels; ++level)
		{
			ComputeContext.SetDynamicDescriptor(1, 0, Environment.GetUAVArray()[level]);

            const UINT mipWidth = MipSize(Environment.GetWidth(), level);
            const UINT mipHeight = MipSize(Environment.GetHeight(), level);
			shaderParams.param1 = level - 1;
			shaderParams.param0 = mipWidth;
			ComputeContext.SetDynamicConstantBufferView(2, sizeof(ShaderParams), &shaderParams);
			ComputeContext.Dispatch(CeilDiv(mipWidth, 32), CeilDiv(mipHeight, 32), 6);

			ComputeContext.TransitionResource(Environment, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		ComputeContext.TransitionResource(Environment, D3D12_RESOURCE_STATE_GENERIC_READ);
	}

	// Compute pre-filtered specular environment map
	{
		__declspec(align(256)) struct SpecularParameters
		{
			float Roughness = 0.0f;
		};
		SpecularParameters specularParameters;
		ComputeContext.SetPipelineState(pipelines["PrefilterSpecularMap"]);
		ComputeContext.SetDynamicDescriptor(0, 0, Environment.GetSRV());
		ComputeContext.TransitionResource(Radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


		const UINT levels = Radiance.GetResource()->GetDesc().MipLevels;
		const float deltaRoughness = 1.0f / std::max(float(levels - 1), (float)1);
		for (UINT level = 0; level < levels; ++level)
		{
            const UINT mipWidth = MipSize(Radiance.GetWidth(), level);
            const UINT mipHeight = MipSize(Radiance.GetHeight(), level);
			const float spmapRoughness = level * deltaRoughness;
			ComputeContext.SetDynamicDescriptor(1, 0, Radiance.GetUAVArray()[level]);
			specularParameters.Roughness = spmapRoughness;
			ComputeContext.SetDynamicConstantBufferView(2, sizeof(SpecularParameters), &specularParameters);
			ComputeContext.Dispatch(CeilDiv(mipWidth, 32), CeilDiv(mipHeight, 32), 6);

			ComputeContext.TransitionResource(Radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		ComputeContext.TransitionResource(Radiance, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	}

	// create irradiance map compute pso
	{

		ComputeContext.SetPipelineState(pipelines["PrecomputeIrradianceMap"]);


		ComputeContext.SetDynamicDescriptor(0, 0, Environment.GetSRV());
		ComputeContext.SetDynamicDescriptor(1, 0, Irradiance.GetUAV());
		ComputeContext.TransitionResource(Irradiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		ComputeContext.Dispatch(CeilDiv(Irradiance.GetWidth(), 32), CeilDiv(Irradiance.GetHeight(), 32), 6);
		ComputeContext.TransitionResource(Irradiance, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}


	{

		ComputeContext.SetPipelineState(pipelines["PrecomputeBRDF"]);

		ComputeContext.SetDynamicDescriptor(1, 0, BrdfLut.GetUAV());
		ComputeContext.TransitionResource(BrdfLut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		ComputeContext.Dispatch(CeilDiv(BrdfLut.GetWidth(), 32), CeilDiv(BrdfLut.GetHeight(), 32), 1);
		ComputeContext.TransitionResource(BrdfLut, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	}

	{

		ComputeContext.SetPipelineState(pipelines["emu"]);
		ComputeContext.SetDynamicDescriptor(1, 0, Emu.GetUAV());
		ComputeContext.TransitionResource(Emu, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(CeilDiv(Emu.GetWidth(), 32), CeilDiv(Emu.GetHeight(), 32), 1);
		ComputeContext.TransitionResource(Emu, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	{
		ComputeContext.SetPipelineState(pipelines["eavg"]);

		ComputeContext.SetDynamicDescriptor(1, 0, Eavg.GetUAV());
		ComputeContext.TransitionResource(Eavg, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(CeilDiv(Eavg.GetWidth(), 32), CeilDiv(Eavg.GetHeight(), 32), 1);
		ComputeContext.TransitionResource(Eavg, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	{
		ComputeContext.SetPipelineState(pipelines["PreintegralDiffuseSSS"]);

		ComputeContext.SetDynamicDescriptor(1, 0, SssDiffuse.GetUAV());
		ComputeContext.TransitionResource(SssDiffuse, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(CeilDiv(SssDiffuse.GetWidth(), 32), CeilDiv(SssDiffuse.GetHeight(), 32), 1);
		ComputeContext.TransitionResource(SssDiffuse, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	{
		ComputeContext.SetPipelineState(pipelines["PreintegralSpecularSSS"]);

		ComputeContext.SetDynamicDescriptor(1, 0, SssSpecular.GetUAV());
		ComputeContext.TransitionResource(SssSpecular, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(CeilDiv(SssSpecular.GetWidth(), 32), CeilDiv(SssSpecular.GetHeight(), 32), 1);
		ComputeContext.TransitionResource(SssSpecular, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	ComputeContext.Finish(true);
}
