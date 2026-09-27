#include "pch.h"
#include "Ssao.h"
#include "RenderGraph.h"

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

    void BuildRandomVectorTexture(CommandContext& context)
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

        context.TransitionResource(g_RandomVectorBuffer, D3D12_RESOURCE_STATE_COPY_DEST, true);
        UpdateSubresources(
            context.GetCommandList(),
            g_RandomVectorBuffer.GetResource(),
            s_RandomVectorMapUploadBuffer.Get(),
            0,
            0,
            num2DSubresources,
            &subResourceData);
        context.TransitionResource(g_RandomVectorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
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
    BuildRandomVectorTexture(initContext);
    BuildSampleKernel();
    initContext.Finish(true);
}

SSAO::ViewPasses::ViewPasses(ViewResources resources) : m_Resources(resources)
{
    m_Graph.AddPass(L"SSAO / Linearize depth", {
        { &m_Resources.Depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.LinearFull, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
    }, [this](CommandContext& commandContext)
    {
        auto& compute = commandContext.GetComputeContext();
        compute.SetConstants(1, 0);
        compute.SetPipelineState(s_LinearizeDepthPso);
        compute.SetDynamicDescriptor(2, 0, m_Resources.LinearFull.GetUAV());
        compute.SetDynamicDescriptor(3, 0, m_Resources.Depth.GetDepthSRV());
        compute.Dispatch2D(m_Resources.LinearFull.GetWidth(), m_Resources.LinearFull.GetHeight(), 8, 8);
    });

    m_Graph.AddPass(L"SSAO / Downsample depth", {
        { &m_Resources.LinearFull, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.LinearHalf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
    }, [this](CommandContext& commandContext)
    {
        auto& compute = commandContext.GetComputeContext();
        compute.SetPipelineState(s_DownsampleDepthPso);
        compute.SetDynamicDescriptor(2, 0, m_Resources.LinearHalf.GetUAV());
        compute.SetDynamicDescriptor(3, 0, m_Resources.LinearFull.GetSRV());
        compute.Dispatch2D(m_Resources.LinearHalf.GetWidth(), m_Resources.LinearHalf.GetHeight(), 8, 8);
    });

    m_Graph.AddPass(L"SSAO / Generate occlusion", {
        { &m_Resources.Normals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.LinearHalf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &g_RandomVectorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.OcclusionHalf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
    }, [this](CommandContext& commandContext)
    {
        auto& compute = commandContext.GetComputeContext();
        compute.SetPipelineState(s_AoRenderPso);
        D3D12_CPU_DESCRIPTOR_HANDLE srvs[3] = {
            m_Resources.Normals.GetSRV(), m_Resources.LinearHalf.GetSRV(), g_RandomVectorBuffer.GetSRV()
        };
        compute.SetDynamicDescriptor(2, 0, m_Resources.OcclusionHalf.GetUAV());
        compute.SetDynamicDescriptors(3, 0, 3, srvs);
        compute.Dispatch2D(m_Resources.OcclusionHalf.GetWidth(), m_Resources.OcclusionHalf.GetHeight(), 8, 8);
    });

    m_Graph.AddPass(L"SSAO / Blur half resolution", {
        { &m_Resources.Normals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.LinearFull, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.LinearHalf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.OcclusionHalf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.Blur, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
    }, [this](CommandContext& commandContext)
    {
        auto& compute = commandContext.GetComputeContext();
        compute.SetConstants(1, 0);
        compute.SetPipelineState(s_AoBlurUpsamplePso);
        D3D12_CPU_DESCRIPTOR_HANDLE srvs[4] = {
            m_Resources.Normals.GetSRV(), m_Resources.LinearFull.GetSRV(), m_Resources.LinearHalf.GetSRV(), m_Resources.OcclusionHalf.GetSRV()
        };
        compute.SetDynamicDescriptor(2, 0, m_Resources.Blur.GetUAV());
        compute.SetDynamicDescriptors(3, 0, 4, srvs);
        compute.Dispatch2D(m_Resources.Blur.GetWidth(), m_Resources.Blur.GetHeight(), 8, 8);
    });

    m_Graph.AddPass(L"SSAO / Upsample", {
        { &m_Resources.Normals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.LinearFull, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.LinearHalf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.Blur, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_Resources.OcclusionFull, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
    }, [this](CommandContext& commandContext)
    {
        auto& compute = commandContext.GetComputeContext();
        compute.SetConstants(1, 1);
        compute.SetPipelineState(s_AoBlurUpsamplePso);
        D3D12_CPU_DESCRIPTOR_HANDLE srvs[4] = {
            m_Resources.Normals.GetSRV(), m_Resources.LinearFull.GetSRV(), m_Resources.LinearHalf.GetSRV(), m_Resources.Blur.GetSRV()
        };
        compute.SetDynamicDescriptor(2, 0, m_Resources.OcclusionFull.GetUAV());
        compute.SetDynamicDescriptors(3, 0, 4, srvs);
        compute.Dispatch2D(m_Resources.OcclusionFull.GetWidth(), m_Resources.OcclusionFull.GetHeight(), 8, 8);
    });

    // Leave the final AO output ready for subsequent graphics passes.
    m_Graph.AddPass(L"SSAO / Publish output", {
        { &m_Resources.OcclusionFull, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, RenderGraph::Access::Read }
    }, {});
}

void SSAO::ViewPasses::Render(GraphicsContext& graphics, const Camera& camera)
{
    SsaoComputeConstants constants = s_SsaoConstants;
    constants.Proj = camera.GetProjMatrix();
    constants.InvProj = Invert(constants.Proj);
    constants.InvSize = XMFLOAT4(
        1.0f / float(m_Resources.OcclusionFull.GetWidth()),
        1.0f / float(m_Resources.OcclusionFull.GetHeight()),
        1.0f / float(m_Resources.OcclusionHalf.GetWidth()),
        1.0f / float(m_Resources.OcclusionHalf.GetHeight()));
    auto& context = graphics.GetComputeContext();
    context.SetRootSignature(s_RootSig);
    context.SetDynamicConstantBufferView(0, sizeof(constants), &constants);
    m_Graph.Execute(context);
}

void SSAO::Render(GraphicsContext& graphics, const Camera& camera)
{
    static ViewPasses legacyView({ g_SceneDepthBuffer, g_SceneNormalBuffer,
        g_LinearDepthFull, g_LinearDepthHalf, g_SSAOFullScreen, g_SSAOHalfRes, g_SSAOBlurTemp });
    legacyView.Render(graphics, camera);
}
void SSAO::Shutdown()
{
    s_RandomVectorMapUploadBuffer.Reset();
}
