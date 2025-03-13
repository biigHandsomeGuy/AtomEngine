#include "pch.h"
#include "Ssao.h"
#include "GraphicsCore.h"
#include "Display.h"
#include "BufferManager.h"

#include "../CompiledShaders/SsaoVS.h"
#include "../CompiledShaders/SsaoPS.h"
#include "../CompiledShaders/SsaoBlurVS.h"
#include "../CompiledShaders/SsaoBlurPS.h"
#include "MathHelper.h"


using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Microsoft::WRL;
using namespace Graphics;

ComPtr<ID3D12Resource> ssaoCbuffer;

namespace
{
    ComPtr<ID3D12RootSignature> s_RootSignature;
    ComPtr<ID3D12PipelineState> s_SsaoPso;
    ComPtr<ID3D12PipelineState> s_BlurPso;
    const UINT64 bufferSize = sizeof(SsaoConstants);
}


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

void SSAO::Render(SsaoConstants& ssaoConstants)
{
    g_CommandList->RSSetViewports(1, &g_ViewPort);
    g_CommandList->RSSetScissorRects(1, &g_Rect);

    // We compute the initial SSAO to AmbientMap0.

    // Change to RENDER_TARGET.
    g_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SSAOFullScreen.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

    float clearValue[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    g_CommandList->ClearRenderTargetView(g_SSAOFullScreenRtvHandle, clearValue, 0, nullptr);

    // Specify the buffers we are going to render to.
    g_CommandList->OMSetRenderTargets(1, &g_SSAOFullScreenRtvHandle, true, nullptr);

    // Bind the constant buffer for this pass.
    
     // material cbuffer
   
    {
       
       
        BYTE* data = nullptr;
        ssaoCbuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
       
        memcpy(data, &ssaoConstants, bufferSize);
        ssaoCbuffer->Unmap(0, nullptr);
    }

    g_CommandList->SetGraphicsRootSignature(s_RootSignature.Get());
    g_CommandList->SetPipelineState(s_SsaoPso.Get());

    g_CommandList->SetGraphicsRootConstantBufferView(0, ssaoCbuffer->GetGPUVirtualAddress());
    g_CommandList->SetGraphicsRoot32BitConstant(1, 0, 0);

    auto normalSrvHandle = GetGpuHandle(g_SrvHeap.Get(), (int)DescriptorHeapLayout::SceneNormalBufferSrv);
    // Bind the normal and depth maps.
    g_CommandList->SetGraphicsRootDescriptorTable(2, normalSrvHandle);


    // Draw fullscreen quad.
    g_CommandList->IASetVertexBuffers(0, 0, nullptr);
    g_CommandList->IASetIndexBuffer(nullptr);
    g_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_CommandList->DrawInstanced(6, 1, 0, 0);

    // Change back to GENERIC_READ so we can read the texture in a shader.
    g_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SSAOFullScreen.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));

}

void SSAO::Initialize()
{
    CD3DX12_DESCRIPTOR_RANGE texTable0; // normal depth
    texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[3];

    // Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstants(1, 1);
    slotRootParameter[2].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);

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
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter,
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

    //
    // PSO for SSAO blur.
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoBlurPsoDesc = ssaoPsoDesc;
    ssaoBlurPsoDesc.VS =
    {
        g_pSsaoBlurVS,sizeof(g_pSsaoBlurVS)
    };
    ssaoBlurPsoDesc.PS =
    {
        g_pSsaoBlurPS,sizeof(g_pSsaoBlurPS)
    };
    

    ThrowIfFailed(g_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(ssaoCbuffer.GetAddressOf())
    ));

    //ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&ssaoBlurPsoDesc, IID_PPV_ARGS(&s_BlurPso)));
}


// void Ssao::BlurAmbientMap(ID3D12GraphicsCommandList* g_CommandList, int blurCount)
// {
//     g_CommandList->SetPipelineState(m_BlurPso);
// 
//     g_CommandList->SetGraphicsRootConstantBufferView(0, ssaoCbuffer->GetGPUVirtualAddress());
// 
// 
//     for (int i = 0; i < blurCount; ++i)
//     {
//         BlurAmbientMap(g_CommandList, true);
//         BlurAmbientMap(g_CommandList, false);
//     }
// }
// 
// void Ssao::BlurAmbientMap(ID3D12GraphicsCommandList* g_CommandList, bool horzBlur)
// {
//     ID3D12Resource* output = nullptr;
//     CD3DX12_GPU_DESCRIPTOR_HANDLE inputSrv;
//     CD3DX12_CPU_DESCRIPTOR_HANDLE outputRtv;
// 
//     // Ping-pong the two ambient map textures as we apply
//     // horizontal and vertical blur passes.
//     if (horzBlur == true)
//     {
//         output = m_SceneColor1.Get();
//         inputSrv = GetGpuHandle(m_SrvHeap.Get(), sceneColor0);
//         outputRtv = m_SceneColor1Rtv;
//         g_CommandList->SetGraphicsRoot32BitConstant(1, 1, 0);
//     }
//     else
//     {
//         output = m_SceneColor0.Get();
//         inputSrv = GetGpuHandle(m_SrvHeap.Get(), sceneColor1);
//         outputRtv = m_SceneColor0Rtv;
//         g_CommandList->SetGraphicsRoot32BitConstant(1, 0, 0);
//     }
// 
//     g_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(output,
//         D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));
// 
//     float clearValue[] = { 1.0f, 1.0f, 1.0f, 1.0f };
//     g_CommandList->ClearRenderTargetView(outputRtv, clearValue, 0, nullptr);
// 
//     g_CommandList->OMSetRenderTargets(1, &outputRtv, true, nullptr);
// 
//     // Normal/depth map still bound.
// 
//     auto normalSrvHandle = GetGpuHandle(m_SrvHeap.Get(), viewNormal);
// 
//     // Bind the normal and depth maps.
//     g_CommandList->SetGraphicsRootDescriptorTable(2, normalSrvHandle);
// 
//     // Bind the input ambient map to second texture table.
//     g_CommandList->SetGraphicsRootDescriptorTable(3, inputSrv);
// 
//     // Draw fullscreen quad.
//     g_CommandList->IASetVertexBuffers(0, 0, nullptr);
//     g_CommandList->IASetIndexBuffer(nullptr);
//     g_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
//     g_CommandList->DrawInstanced(6, 1, 0, 0);
// 
//     g_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(output,
//         D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
// }



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

        XMStoreFloat4(&mOffsets[i], v);
    }
}