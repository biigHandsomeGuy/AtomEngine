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

    ComPtr<ID3D12RootSignature> s_RootSignature;
    ComPtr<ID3D12PipelineState> s_SsaoPso;
    ComPtr<ID3D12PipelineState> s_BlurPso;
    ComPtr<ID3D12Resource> s_RandomVectorMapUploadBuffer;

    ComPtr<ID3D12RootSignature> s_ComputeRS;
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

    CD3DX12_DESCRIPTOR_RANGE texTable0; // normal depth
    CD3DX12_DESCRIPTOR_RANGE texTable1; // random
    texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);
    texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);


    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    // Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstants(1, 1);
    slotRootParameter[2].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[3].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
        0.0f,
        0,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
    {
        pointClamp, linearClamp, depthMapSam, linearWrap
    };

    // A root signature is an array of root parameters.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(g_Device->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&s_RootSignature)));

    //
 // PSO for SSAO.  
 //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoPsoDesc = {};

    ssaoPsoDesc.InputLayout = { nullptr, 0 };
    ssaoPsoDesc.pRootSignature = s_RootSignature.Get();
    ssaoPsoDesc.VS =
    {
        g_pSsaoVS,sizeof(g_pSsaoVS)
    };
    ssaoPsoDesc.PS =
    {
        g_pSsaoPS,sizeof(g_pSsaoPS)
    };
    ssaoPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    ssaoPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    ssaoPsoDesc.BlendState.RenderTarget[0].BlendEnable = false;
    ssaoPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    ssaoPsoDesc.SampleMask = UINT_MAX;
    ssaoPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    ssaoPsoDesc.NumRenderTargets = 1;
    // SSAO effect does not need the depth buffer.
    ssaoPsoDesc.DepthStencilState.DepthEnable = false;
    ssaoPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ssaoPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
    ssaoPsoDesc.SampleDesc.Count = 1;
    ssaoPsoDesc.SampleDesc.Quality = 0;
    ssaoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&ssaoPsoDesc, IID_PPV_ARGS(&s_SsaoPso)));


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
    // universal conpute root signature

    CD3DX12_DESCRIPTOR_RANGE range1 = {};
    range1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);
    CD3DX12_DESCRIPTOR_RANGE range2 = {};
    range2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);
    CD3DX12_DESCRIPTOR_RANGE range3 = {};
    range3.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

    CD3DX12_ROOT_PARAMETER rootParameter[5] = {};
    rootParameter[0].InitAsDescriptorTable(1, &range1);
    rootParameter[1].InitAsDescriptorTable(1, &range2);
    rootParameter[2].InitAsDescriptorTable(1, &range3);
    rootParameter[3].InitAsConstantBufferView(0, 0);
    rootParameter[4].InitAsConstants(1, 1);

    //auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc1(5, rootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    
    ComPtr<ID3DBlob> serializedRootSig1 = nullptr;
    ComPtr<ID3DBlob> errorBlob1 = nullptr;

    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootSigDesc1, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig1.GetAddressOf(), errorBlob1.GetAddressOf()));

    if (errorBlob1 != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob1->GetBufferPointer());
    }


    ThrowIfFailed(g_Device->CreateRootSignature(
        0,
        serializedRootSig1->GetBufferPointer(),
        serializedRootSig1->GetBufferSize(),
        IID_PPV_ARGS(&s_ComputeRS)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.CS =
    {
        g_pSsaoBlurCS,sizeof(g_pSsaoBlurCS)
    };
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    psoDesc.pRootSignature = s_ComputeRS.Get();
    ThrowIfFailed(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&s_BlurPso)));

    
    gfxContext.Finish();
}

#define OffsetHandle(x) CD3DX12_GPU_DESCRIPTOR_HANDLE(Renderer::g_SSAOSrvHeap, x, CbvSrvUavDescriptorSize)

void SSAO::Render(CommandContext& GfxContext, const Camera& camera)
{
    {

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

        g_Device->CopyDescriptors(1, &Renderer::g_SSAOSrvHeap, &DestCount, DestCount, SourceTextures,
            SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_Device->CopyDescriptorsSimple(1, Renderer::g_SSAOUavHeap, g_SSAOFullScreen.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    s_ViewPort = { 0,0,g_DisplayWidth / 2.0f,g_DisplayHeight / 2.0f,0,1 };
    s_Rect = { 0,0,(long)g_DisplayWidth / 2,(long)g_DisplayHeight / 2 };

    GfxContext.GetCommandList()->RSSetViewports(1, &s_ViewPort);
    GfxContext.GetCommandList()->RSSetScissorRects(1, &s_Rect);



    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        g_SSAOFullScreen.GetResource(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));

    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        g_SceneDepthBuffer.GetResource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE));


    float clearValue[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    GfxContext.GetCommandList()->ClearRenderTargetView(g_SSAOFullScreen.GetRTV(), clearValue, 0, nullptr);

    // Specify the buffers we are going to render to.
    GfxContext.GetCommandList()->OMSetRenderTargets(1, &g_SSAOFullScreen.GetRTV(), true, nullptr);

  
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

    GfxContext.GetCommandList()->SetGraphicsRootSignature(s_RootSignature.Get());
    GfxContext.GetCommandList()->SetPipelineState(s_SsaoPso.Get());

    GfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(0, s_SsaoCbuffer->GetGPUVirtualAddress());
    GfxContext.GetCommandList()->SetGraphicsRoot32BitConstant(1, 0, 0);

    // Bind the normal and depth maps.
    GfxContext.GetCommandList()->SetGraphicsRootDescriptorTable(2, OffsetHandle(0));

    GfxContext.GetCommandList()->SetGraphicsRootDescriptorTable(3, OffsetHandle(2));


    // Draw fullscreen quad.
    GfxContext.GetCommandList()->IASetVertexBuffers(0, 0, nullptr);
    GfxContext.GetCommandList()->IASetIndexBuffer(nullptr);
    GfxContext.GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    GfxContext.GetCommandList()->DrawInstanced(6, 1, 0, 0);

    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        g_SSAOFullScreen.GetResource(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON));


    // first blur - vertical
    //{
    //    GfxContext.GetCommandList()->SetComputeRootSignature(s_ComputeRS.Get());
    //    GfxContext.GetCommandList()->SetPipelineState(s_BlurPso.Get());
    //
    //    GfxContext.GetCommandList()->SetComputeRootDescriptorTable(0, OffsetHandle(0));
    //
    //    GfxContext.GetCommandList()->SetComputeRootDescriptorTable(1, OffsetHandle(3));
    //    GfxContext.GetCommandList()->SetComputeRootDescriptorTable(2, Renderer::g_SSAOUavHeap);
    //
    //    GfxContext.GetCommandList()->SetComputeRootConstantBufferView(3, s_SsaoCbuffer->GetGPUVirtualAddress());
    //    GfxContext.GetCommandList()->SetComputeRoot32BitConstant(4, 0, 0);
    //
    //    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
    //        g_SSAOFullScreen.GetResource(), 
    //        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    //
    //    GfxContext.GetCommandList()->Dispatch((g_DisplayWidth + 31) / 32, (g_DisplayHeight + 31) / 32, 1);
    //    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
    //        g_SSAOFullScreen.GetResource(), 
    //        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
    //}
    //GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
    //    g_SSAOUnBlur.GetResource(),
    //    D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));

    
    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        g_SceneDepthBuffer.GetResource(),
        D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));


    //// first blur - horizontal
    //{
    //    GfxContext.GetCommandList()->SetComputeRootDescriptorTable(1, OffsetHandle(4));
    //    GfxContext.GetCommandList()->SetComputeRootDescriptorTable(2, UavHandle);
    //
    //    GfxContext.GetCommandList()->SetComputeRoot32BitConstant(4, 1, 0);
    //
    //    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SSAOFullScreen.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    //
    //    GfxContext.GetCommandList()->Dispatch((g_DisplayWidth + 31) / 32, (g_DisplayHeight + 31) / 32, 1);
    //    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SSAOFullScreen.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
    //}
    //
    //// second blur
    //{
    //    auto ssaoUnBlurSrvHandle = GetGpuHandle(g_SrvHeap.Get(), (int)DescriptorHeapLayout::SsaoMapHeap);
    //    GfxContext.GetCommandList()->SetComputeRootDescriptorTable(1, ssaoUnBlurSrvHandle);
    //    GfxContext.GetCommandList()->SetComputeRoot32BitConstant(4, 0, 0);
    //
    //    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SSAOFullScreen.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    //
    //    GfxContext.GetCommandList()->Dispatch((g_DisplayWidth + 31) / 32, (g_DisplayHeight + 31) / 32, 1);
    //    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SSAOFullScreen.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
    //}
    //{
    //    auto ssaoUnBlurSrvHandle = GetGpuHandle(g_SrvHeap.Get(), (int)DescriptorHeapLayout::SsaoMapHeap);
    //    GfxContext.GetCommandList()->SetComputeRootDescriptorTable(1, ssaoUnBlurSrvHandle);
    //    GfxContext.GetCommandList()->SetComputeRoot32BitConstant(4, 1, 0);
    //
    //    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SSAOFullScreen.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    //
    //    GfxContext.GetCommandList()->Dispatch((g_DisplayWidth + 31) / 32, (g_DisplayHeight + 31) / 32, 1);
    //    GfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SSAOFullScreen.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
    //}

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
    s_RootSignature.Reset();
    s_SsaoPso.Reset();
    s_BlurPso.Reset();
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
