#include "pch.h"
#include "ColorBuffer.h"
#include "GraphicsCommon.h"
#include "GraphicsCore.h"
#include "CommandContext.h"


void ColorBuffer::CreateDerivedViews(ID3D12Device* Device, DXGI_FORMAT Format, uint32_t ArraySize, uint32_t NumMips)
{
    //assert(ArraySize == 1 || NumMips == 1, "We don't support auto-mips on texture arrays");

    m_NumMipMaps = NumMips - 1;

    D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc[12] = {};
    D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};

    RTVDesc.Format = Format;
    for (int i = 0; i < 12; i++)
    {
        UAVDesc[i].Format = GetUAVFormat(Format);
    }
    SRVDesc.Format = Format;
    SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    if (ArraySize > 1)
    {
        RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        RTVDesc.Texture2DArray.MipSlice = 0;
        RTVDesc.Texture2DArray.FirstArraySlice = 0;
        RTVDesc.Texture2DArray.ArraySize = (UINT)ArraySize;

        for (int i = 0; i < NumMips; i++)
        {
            UAVDesc[i].ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            UAVDesc[i].Texture2DArray.MipSlice = i;
            UAVDesc[i].Texture2DArray.FirstArraySlice = 0;
            UAVDesc[i].Texture2DArray.ArraySize = (UINT)ArraySize;
        }

        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        SRVDesc.TextureCube.MipLevels = -1;
        SRVDesc.TextureCube.MostDetailedMip = 0;
    }
    else if (m_FragmentCount > 1)
    {
        RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    }
    else
    {
        RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        RTVDesc.Texture2D.MipSlice = 0;

        UAVDesc[0].ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        UAVDesc[0].Texture2D.MipSlice = 0;

        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MipLevels = -1;
        SRVDesc.Texture2D.MostDetailedMip = 0;
    }

    if (m_SRVHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
    {
        m_RTVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_SRVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ID3D12Resource* Resource = m_pResource.Get();

    // Create the render target view
    Device->CreateRenderTargetView(Resource, &RTVDesc, m_RTVHandle);

    // Create the shader resource view
    Device->CreateShaderResourceView(Resource, &SRVDesc, m_SRVHandle);

    if (m_FragmentCount > 1)
        return;

    // Create the UAVs for each mip level (RWTexture2D)
    for (uint32_t i = 0; i < NumMips; ++i)
    {
        if (m_UAVHandle[i].ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
            m_UAVHandle[i] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        Device->CreateUnorderedAccessView(Resource, nullptr, &UAVDesc[i], m_UAVHandle[i]);

    }
}

void ColorBuffer::CreateFromSwapChain(const std::wstring& Name, ID3D12Resource* BaseResource)
{
    AssociateWithResource(Graphics::g_Device, Name, BaseResource, D3D12_RESOURCE_STATE_PRESENT);

    //m_UAVHandle[0] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    //Graphics::g_Device->CreateUnorderedAccessView(m_pResource.Get(), nullptr, nullptr, m_UAVHandle[0]);

    m_RTVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    Graphics::g_Device->CreateRenderTargetView(m_pResource.Get(), nullptr, m_RTVHandle);
}

void ColorBuffer::Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t NumMips,
    DXGI_FORMAT Format)
{
    NumMips = (NumMips == 0 ? ComputeNumMips(Width, Height) : NumMips);
    D3D12_RESOURCE_FLAGS Flags = CombineResourceFlags();
    D3D12_RESOURCE_DESC ResourceDesc = DescribeTex2D(Width, Height, 1, NumMips, Format, Flags);

    ResourceDesc.SampleDesc.Count = m_FragmentCount;
    ResourceDesc.SampleDesc.Quality = 0;

    D3D12_CLEAR_VALUE ClearValue = {};
    ClearValue.Format = Format;
    ClearValue.Color[0] = m_ClearColor.R();
    ClearValue.Color[1] = m_ClearColor.G();
    ClearValue.Color[2] = m_ClearColor.B();
    ClearValue.Color[3] = m_ClearColor.A();

    CreateTextureResource(Graphics::g_Device, Name, ResourceDesc, ClearValue);
    CreateDerivedViews(Graphics::g_Device, Format, 1, NumMips);
}

void ColorBuffer::CreateArray(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t ArrayCount, uint32_t NumMips,
    DXGI_FORMAT Format)
{
    D3D12_RESOURCE_FLAGS Flags = CombineResourceFlags();
    D3D12_RESOURCE_DESC ResourceDesc = DescribeTex2D(Width, Height, ArrayCount, NumMips, Format, Flags);

    D3D12_CLEAR_VALUE ClearValue = {};
    ClearValue.Format = Format;
    ClearValue.Color[0] = m_ClearColor.R();
    ClearValue.Color[1] = m_ClearColor.G();
    ClearValue.Color[2] = m_ClearColor.B();
    ClearValue.Color[3] = m_ClearColor.A();

    CreateTextureResource(Graphics::g_Device, Name, ResourceDesc, ClearValue);
    CreateDerivedViews(Graphics::g_Device, Format, ArrayCount, NumMips);
}
