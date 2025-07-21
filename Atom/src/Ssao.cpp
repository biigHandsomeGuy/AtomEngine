#include "pch.h"
#include "Ssao.h"
#include "GraphicsCore.h"
#include "Display.h"
#include "BufferManager.h"
#include "MathHelper.h"
#include "Camera.h"
#include "CommandContext.h"
#include "Renderer.h"
#include "DescriptorHeap.h"
#include "RootSignature.h"
#include "GraphicsCommon.h"
#include "PipelineState.h"

namespace shader
{
#include "../CompiledShaders/SsaoVS.h"
#include "../CompiledShaders/SsaoPS.h"
#include "../CompiledShaders/SsaoBlurCS.h"
}

using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Microsoft::WRL;
using namespace Graphics;
using namespace shader;

namespace
{

    RootSignature m_RootSig;
    GraphicsPSO s_SsaoPso(L"SSAO PSO");

    ComPtr<ID3D12Resource> s_RandomVectorMapUploadBuffer;

    const UINT64 s_SsaoCbuferSize = sizeof(SsaoConstants);

    SsaoConstants s_SsaoConstants = {};
    ComPtr<ID3D12Resource> s_SsaoCbuffer;

    D3D12_VIEWPORT s_ViewPort;
    D3D12_RECT s_Rect;
}
void BuildOffsetVectors();
void BuildRandomVectorTexture(ID3D12GraphicsCommandList*);
std::vector<float> CalcGaussWeights(float sigma)
{
    float twoSigma2 = 2.0f * sigma * sigma;

    // Estimate the blur radius based on sigma since sigma controls the "width" of the bell curve.
    // For example, for sigma = 3, the width of the bell curve is 
    int blurRadius = (int)ceil(2.0f * sigma);

    std::vector<float> weights;
    weights.resize(2 * blurRadius + 1);

    float weightSum = 0.0f;

    for (int i = -blurRadius; i <= blurRadius; ++i)
    {
        float x = (float)i;

        weights[i + blurRadius] = expf(-x * x / twoSigma2);

        weightSum += weights[i + blurRadius];
    }

    // Divide by the sum so all the weights add up to 1.0.
    for (int i = 0; i < weights.size(); ++i)
    {
        weights[i] /= weightSum;
    }

    return weights;
}

void SSAO::Initialize()
{
    CommandContext& gfxContext = CommandContext::Begin(L"SSAO Initialize");

    s_ViewPort = { 0,0,g_DisplayWidth / 2.0f,g_DisplayHeight / 2.0f,0,1 };
    s_Rect = { 0,0,(long)g_DisplayWidth / 2,(long)g_DisplayHeight / 2 };

    m_RootSig.Reset(4, 4);
    m_RootSig.InitStaticSampler(0, SamplerPointClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig.InitStaticSampler(1, SamplerLinearClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig.InitStaticSampler(2, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig.InitStaticSampler(3, SamplerLinearWrapDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[0].InitAsConstantBuffer(0);
    m_RootSig[1].InitAsConstants(1, 1);
    m_RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
    m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1);
    m_RootSig.Finalize(L"SSAO_RootSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    DXGI_FORMAT Format = DXGI_FORMAT_R8_UNORM;

    s_SsaoPso.SetRootSignature(m_RootSig);
    s_SsaoPso.SetRasterizerState(RasterizerDefault);
    s_SsaoPso.SetBlendState(BlendNoColorWrite);
    s_SsaoPso.SetDepthStencilState(DepthStateDisabled);
    s_SsaoPso.SetInputLayout(0, nullptr);
    s_SsaoPso.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    s_SsaoPso.SetRenderTargetFormats(1, &Format, DXGI_FORMAT_UNKNOWN);
    s_SsaoPso.SetVertexShader(g_pSsaoVS, sizeof(g_pSsaoVS));
    s_SsaoPso.SetPixelShader(g_pSsaoPS, sizeof(g_pSsaoPS));
    s_SsaoPso.Finalize();

 

    ThrowIfFailed(g_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(s_SsaoCbuferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(s_SsaoCbuffer.GetAddressOf())
    ));
    BuildRandomVectorTexture(gfxContext.GetCommandList());
    BuildOffsetVectors();

    
    gfxContext.Finish();
}


#define OffsetHandle(x) CD3DX12_GPU_DESCRIPTOR_HANDLE(Renderer::g_SSAOSrvHeap, x, CbvSrvUavDescriptorSize)

void SSAO::Render(GraphicsContext& GfxContext, const Camera& camera)
{
    {

        uint32_t DestCount = 4;
        uint32_t SourceCounts[] = { 1, 1, 1, 1 };

        D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
        {
            g_SceneNormalBuffer.GetSRV(),
            g_SceneDepthBuffer.GetDepthSRV(),
            g_RandomVectorBuffer.GetSRV(),
            g_SSAOFullScreen.GetSRV(),
        };

        g_Device->CopyDescriptors(1, &Renderer::g_SSAOSrvHeap, &DestCount, DestCount, SourceTextures,
            SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_Device->CopyDescriptorsSimple(1, Renderer::g_SSAOUavHeap, g_SSAOFullScreen.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    s_ViewPort = { 0,0,g_DisplayWidth / 2.0f,g_DisplayHeight / 2.0f,0,1 };
    s_Rect = { 0,0,(long)g_DisplayWidth / 2,(long)g_DisplayHeight / 2 };

    GfxContext.SetViewportAndScissor(s_ViewPort, s_Rect);

    GfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_RENDER_TARGET);

    GfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

    GfxContext.ClearColor(g_SSAOFullScreen);

    GfxContext.SetRenderTarget(g_SSAOFullScreen.GetRTV());

  
    {
        XMMATRIX P = camera.GetProj();

        // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
        XMMATRIX T(
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, -0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f);

        XMStoreFloat4x4(&s_SsaoConstants.Proj, P);
        XMMATRIX invProj = XMMatrixInverse(nullptr, P);
        XMStoreFloat4x4(&s_SsaoConstants.InvProj, invProj);

        XMStoreFloat4x4(&s_SsaoConstants.ProjTex, P * T);

        
        auto blurWeights = CalcGaussWeights(2.5f);
        s_SsaoConstants.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
        s_SsaoConstants.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
        s_SsaoConstants.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);
        
        s_SsaoConstants.InvRenderTargetSize = XMFLOAT2((1.0f / g_DisplayWidth * 2), 1.0f / g_DisplayHeight * 2);

        
        BYTE* data = nullptr;
        s_SsaoCbuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
        
        memcpy(data, &s_SsaoConstants, s_SsaoCbuferSize);
        s_SsaoCbuffer->Unmap(0, nullptr);
    }

    GfxContext.SetRootSignature(m_RootSig);
    GfxContext.SetPipelineState(s_SsaoPso);

    GfxContext.SetConstantBuffer(0, s_SsaoCbuffer->GetGPUVirtualAddress());
    GfxContext.SetConstant(1, 0, 0);

    // Bind the normal and depth maps.
    GfxContext.SetDescriptorTable(2, OffsetHandle(0));
    GfxContext.SetDescriptorTable(3, OffsetHandle(2));


    GfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    GfxContext.DrawInstanced(6, 1, 0, 0);

    GfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_COMMON);

    GfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}


void BuildRandomVectorTexture(ID3D12GraphicsCommandList* CmdList)
{
    g_RandomVectorBuffer.Create(L"Random Vector Buffer", 256, 256, 1, DXGI_FORMAT_R8G8B8A8_UNORM);

    auto texDesc = g_RandomVectorBuffer.GetResource()->GetDesc();

    const UINT num2DSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(g_RandomVectorBuffer.GetResource(),  0, num2DSubresources);

    ThrowIfFailed(g_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(s_RandomVectorMapUploadBuffer.GetAddressOf())));

    XMCOLOR initData[256 * 256];
    for (int i = 0; i < 256; ++i)
    {
        for (int j = 0; j < 256; ++j)
        {
            // Random vector in [0,1].  We will decompress in shader to [-1,1].
            XMFLOAT3 v(MathHelper::RandF(), MathHelper::RandF(), MathHelper::RandF());

            initData[i * 256 + j] = XMCOLOR(v.x, v.y, v.z, 0.0f);
        }
    }

    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = initData;
    subResourceData.RowPitch = 256 * sizeof(XMCOLOR);
    subResourceData.SlicePitch = subResourceData.RowPitch * 256;

    //
    // Schedule to copy the data to the default resource, and change states.
    // Note that mCurrSol is put in the GENERIC_READ state so it can be 
    // read by a shader.
    //

    CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_RandomVectorBuffer.GetResource(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
    UpdateSubresources(CmdList, g_RandomVectorBuffer.GetResource(), s_RandomVectorMapUploadBuffer.Get(),
        0, 0, num2DSubresources, &subResourceData);
    CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_RandomVectorBuffer.GetResource(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));
}

void SSAO::Shutdown()
{
    s_SsaoCbuffer.Reset();
}

void BuildOffsetVectors()
{
    // Start with 14 uniformly distributed vectors.  We choose the 8 corners of the cube
    // and the 6 center points along each cube face.  We always alternate the points on 
    // opposites sides of the cubes.  This way we still get the vectors spread out even
    // if we choose to use less than 14 samples.
    DirectX::XMFLOAT4 mOffsets[14];

    // 8 cube corners
    mOffsets[0] = XMFLOAT4(+1.0f, +1.0f, +1.0f, 0.0f);
    mOffsets[1] = XMFLOAT4(-1.0f, -1.0f, -1.0f, 0.0f);

    mOffsets[2] = XMFLOAT4(-1.0f, +1.0f, +1.0f, 0.0f);
    mOffsets[3] = XMFLOAT4(+1.0f, -1.0f, -1.0f, 0.0f);

    mOffsets[4] = XMFLOAT4(+1.0f, +1.0f, -1.0f, 0.0f);
    mOffsets[5] = XMFLOAT4(-1.0f, -1.0f, +1.0f, 0.0f);

    mOffsets[6] = XMFLOAT4(-1.0f, +1.0f, -1.0f, 0.0f);
    mOffsets[7] = XMFLOAT4(+1.0f, -1.0f, +1.0f, 0.0f);

    // 6 centers of cube faces
    mOffsets[8] = XMFLOAT4(-1.0f, 0.0f, 0.0f, 0.0f);
    mOffsets[9] = XMFLOAT4(+1.0f, 0.0f, 0.0f, 0.0f);

    mOffsets[10] = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f);
    mOffsets[11] = XMFLOAT4(0.0f, +1.0f, 0.0f, 0.0f);

    mOffsets[12] = XMFLOAT4(0.0f, 0.0f, -1.0f, 0.0f);
    mOffsets[13] = XMFLOAT4(0.0f, 0.0f, +1.0f, 0.0f);

    for (int i = 0; i < 14; ++i)
    {
        // Create random lengths in [0.25, 1.0].
        float s = MathHelper::RandF(0.25f, 1.0f);

        XMVECTOR v = s * XMVector4Normalize(XMLoadFloat4(&mOffsets[i]));

        XMStoreFloat4(&s_SsaoConstants.OffsetVectors[i], v);
    }
}
