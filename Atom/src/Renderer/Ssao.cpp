#include "pch.h"
#include "Ssao.h"

#include "BufferManager.h"
#include "Camera.h"
#include "CommandContext.h"
#include "GraphicsCommon.h"
#include "GraphicsCore.h"
#include "MathHelper.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include <wrl/client.h>

namespace shader
{
#include "../CompiledShaders/LinearizeDepthCS.h"
#include "../CompiledShaders/DownsampleDepthCS.h"
#include "../CompiledShaders/AoRenderCS.h"
#include "../CompiledShaders/AoBlurUpsampleCS.h"
}

using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Graphics;
using namespace shader;
using namespace Math;
using Microsoft::WRL::ComPtr;

namespace
{
    struct SsaoComputeConstants
    {
        Matrix4 Proj = Matrix4(kIdentity);
        Matrix4 InvProj = Matrix4(kIdentity);
        Vector4 SampleKernel[16];
        XMFLOAT4 InvSize = { 0.0f, 0.0f, 0.0f, 0.0f };
        XMFLOAT4 AoParams = { 1.5f, 2.0f, 0.02f, 2.0f };
    };

    RootSignature s_RootSig;
    ComputePSO s_LinearizeDepthPso(L"SSAO Linearize Depth");
    ComputePSO s_DownsampleDepthPso(L"SSAO Downsample Depth");
    ComputePSO s_AoRenderPso(L"SSAO Render");
    ComputePSO s_AoBlurUpsamplePso(L"SSAO Blur Upsample");

    ComPtr<ID3D12Resource> s_RandomVectorMapUploadBuffer;
    SsaoComputeConstants s_SsaoConstants;
}

namespace
{
    void BuildSampleKernel()
    {
        for (uint32_t i = 0; i < _countof(s_SsaoConstants.SampleKernel); ++i)
        {
            Vector3 sample(
                MathHelper::RandF(-1.0f, 1.0f),
                MathHelper::RandF(-1.0f, 1.0f),
                MathHelper::RandF(0.1f, 1.0f));

            sample = Normalize(sample);

            float scale = float(i + 1) / float(_countof(s_SsaoConstants.SampleKernel));
            scale = MathHelper::Lerp(0.1f, 1.0f, scale * scale);
            sample = sample * scale;

            s_SsaoConstants.SampleKernel[i] = Vector4(sample.GetX(), sample.GetY(), sample.GetZ(), 0.0f);
        }
    }

    void BuildRandomVectorTexture(ID3D12GraphicsCommandList* CmdList)
    {
        g_RandomVectorBuffer.Create(L"Random Vector Buffer", 256, 256, 1, DXGI_FORMAT_R8G8B8A8_UNORM);

        auto texDesc = g_RandomVectorBuffer.GetResource()->GetDesc();
        const UINT num2DSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(g_RandomVectorBuffer.GetResource(), 0, num2DSubresources);

        ASSERT_SUCCEEDED(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(s_RandomVectorMapUploadBuffer.GetAddressOf())));

        XMCOLOR initData[256 * 256];
        for (uint32_t y = 0; y < 256; ++y)
        {
            for (uint32_t x = 0; x < 256; ++x)
            {
                XMFLOAT3 v(MathHelper::RandF(), MathHelper::RandF(), MathHelper::RandF());
                initData[y * 256 + x] = XMCOLOR(v.x, v.y, v.z, 1.0f);
            }
        }

        D3D12_SUBRESOURCE_DATA subResourceData = {};
        subResourceData.pData = initData;
        subResourceData.RowPitch = 256 * sizeof(XMCOLOR);
        subResourceData.SlicePitch = subResourceData.RowPitch * 256;

        CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            g_RandomVectorBuffer.GetResource(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_DEST));
        UpdateSubresources(
            CmdList,
            g_RandomVectorBuffer.GetResource(),
            s_RandomVectorMapUploadBuffer.Get(),
            0,
            0,
            num2DSubresources,
            &subResourceData);
        CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            g_RandomVectorBuffer.GetResource(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_GENERIC_READ));
    }
}

void SSAO::Initialize()
{
    s_RootSig.Reset(4, 2);
    s_RootSig.InitStaticSampler(0, SamplerPointClampDesc);
    s_RootSig.InitStaticSampler(1, SamplerLinearClampDesc);
    s_RootSig[0].InitAsConstantBuffer(0);
    s_RootSig[1].InitAsConstants(1, 1);
    s_RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    s_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4);
    s_RootSig.Finalize(L"SSAO Compute Root Sig");

    s_LinearizeDepthPso.SetRootSignature(s_RootSig);
    s_LinearizeDepthPso.SetComputeShader(g_pLinearizeDepthCS, sizeof(g_pLinearizeDepthCS));
    s_LinearizeDepthPso.Finalize();

    s_DownsampleDepthPso.SetRootSignature(s_RootSig);
    s_DownsampleDepthPso.SetComputeShader(g_pDownsampleDepthCS, sizeof(g_pDownsampleDepthCS));
    s_DownsampleDepthPso.Finalize();

    s_AoRenderPso.SetRootSignature(s_RootSig);
    s_AoRenderPso.SetComputeShader(g_pAoRenderCS, sizeof(g_pAoRenderCS));
    s_AoRenderPso.Finalize();

    s_AoBlurUpsamplePso.SetRootSignature(s_RootSig);
    s_AoBlurUpsamplePso.SetComputeShader(g_pAoBlurUpsampleCS, sizeof(g_pAoBlurUpsampleCS));
    s_AoBlurUpsamplePso.Finalize();

    CommandContext& initContext = CommandContext::Begin(L"SSAO Initialize");
    BuildRandomVectorTexture(initContext.GetCommandList());
    BuildSampleKernel();
    initContext.Finish();
}

void SSAO::Render(GraphicsContext& GfxContext, const Camera& camera)
{
    s_SsaoConstants.Proj = camera.GetProjMatrix();
    s_SsaoConstants.InvProj = Invert(s_SsaoConstants.Proj);
    s_SsaoConstants.InvSize = XMFLOAT4(
        1.0f / float(g_SSAOFullScreen.GetWidth()),
        1.0f / float(g_SSAOFullScreen.GetHeight()),
        1.0f / float(g_SSAOHalfRes.GetWidth()),
        1.0f / float(g_SSAOHalfRes.GetHeight()));

    ComputeContext& Context = GfxContext.GetComputeContext();
    Context.SetRootSignature(s_RootSig);
    Context.SetDynamicConstantBufferView(0, sizeof(s_SsaoConstants), &s_SsaoConstants);

    Context.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Context.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Context.TransitionResource(g_RandomVectorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    Context.SetConstants(1, 0);
    Context.SetPipelineState(s_LinearizeDepthPso);
    Context.TransitionResource(g_LinearDepthFull, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Context.SetDynamicDescriptor(2, 0, g_LinearDepthFull.GetUAV());
    Context.SetDynamicDescriptor(3, 0, g_SceneDepthBuffer.GetDepthSRV());
    Context.Dispatch2D(g_LinearDepthFull.GetWidth(), g_LinearDepthFull.GetHeight(), 8, 8);

    Context.SetPipelineState(s_DownsampleDepthPso);
    Context.TransitionResource(g_LinearDepthFull, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Context.TransitionResource(g_LinearDepthHalf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Context.SetDynamicDescriptor(2, 0, g_LinearDepthHalf.GetUAV());
    Context.SetDynamicDescriptor(3, 0, g_LinearDepthFull.GetSRV());
    Context.Dispatch2D(g_LinearDepthHalf.GetWidth(), g_LinearDepthHalf.GetHeight(), 8, 8);

    Context.SetPipelineState(s_AoRenderPso);
    Context.TransitionResource(g_LinearDepthHalf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Context.TransitionResource(g_SSAOHalfRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_CPU_DESCRIPTOR_HANDLE aoRenderSrvs[3] =
    {
        g_SceneNormalBuffer.GetSRV(),
        g_LinearDepthHalf.GetSRV(),
        g_RandomVectorBuffer.GetSRV()
    };
    Context.SetDynamicDescriptor(2, 0, g_SSAOHalfRes.GetUAV());
    Context.SetDynamicDescriptors(3, 0, 3, aoRenderSrvs);
    Context.Dispatch2D(g_SSAOHalfRes.GetWidth(), g_SSAOHalfRes.GetHeight(), 8, 8);

    Context.SetPipelineState(s_AoBlurUpsamplePso);
    Context.SetConstants(1, 0);
    Context.TransitionResource(g_SSAOHalfRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Context.TransitionResource(g_SSAOBlurTemp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_CPU_DESCRIPTOR_HANDLE blurSrvs[4] =
    {
        g_SceneNormalBuffer.GetSRV(),
        g_LinearDepthFull.GetSRV(),
        g_LinearDepthHalf.GetSRV(),
        g_SSAOHalfRes.GetSRV()
    };
    Context.SetDynamicDescriptor(2, 0, g_SSAOBlurTemp.GetUAV());
    Context.SetDynamicDescriptors(3, 0, 4, blurSrvs);
    Context.Dispatch2D(g_SSAOBlurTemp.GetWidth(), g_SSAOBlurTemp.GetHeight(), 8, 8);

    Context.SetConstants(1, 1);
    Context.TransitionResource(g_SSAOBlurTemp, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Context.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_CPU_DESCRIPTOR_HANDLE upsampleSrvs[4] =
    {
        g_SceneNormalBuffer.GetSRV(),
        g_LinearDepthFull.GetSRV(),
        g_LinearDepthHalf.GetSRV(),
        g_SSAOBlurTemp.GetSRV()
    };
    Context.SetDynamicDescriptor(2, 0, g_SSAOFullScreen.GetUAV());
    Context.SetDynamicDescriptors(3, 0, 4, upsampleSrvs);
    Context.Dispatch2D(g_SSAOFullScreen.GetWidth(), g_SSAOFullScreen.GetHeight(), 8, 8);

    Context.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

void SSAO::Shutdown()
{
    s_RandomVectorMapUploadBuffer.Reset();
}
