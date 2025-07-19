#pragma once

#include "PixelBuffer.h"
#include "Color.h"


class ColorBuffer : public PixelBuffer
{
public:
    ColorBuffer(Color clearColor = Color(0.0f, 0.0f, 0.0f, 0.0f))
        :m_ClearColor(clearColor), m_NumMipMaps(0), m_FragmentCount(1), m_SampleCount(1)
    {
        m_RTVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_SRVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        for (int i = 0; i < _countof(m_UAVHandle); ++i)
            m_UAVHandle[i].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
    }

    // Create a color buffer from a swap chain buffer.  Unordered access is restricted.
    void CreateFromSwapChain(const std::wstring& name, ID3D12Resource* baseResource);

    void Create(const std::wstring& name, uint32_t width, uint32_t height, uint32_t numMips, DXGI_FORMAT format);
    void CreateArray(const std::wstring& name, uint32_t width, uint32_t height, uint32_t arrayCount,uint32_t numMips, DXGI_FORMAT format);

    // Get pre-created CPU-visible descriptor handles
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV(void) const { return m_SRVHandle; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetRTV(void) const { return m_RTVHandle; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV(void) const { return m_UAVHandle[0]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE* GetUAVArray(void) const { return m_UAVHandle; }
    Color GetClearColor(void) const { return m_ClearColor; }


protected:
    void CreateDerivedViews(ID3D12Device* device, DXGI_FORMAT format, uint32_t arraySize, uint32_t numMips = 1);

    
protected:
    // Compute the number of texture levels needed to reduce to 1x1.  This uses
    // _BitScanReverse to find the highest set bit.  Each dimension reduces by
    // half and truncates bits.  The dimension 256 (0x100) has 9 mip levels, same
    // as the dimension 511 (0x1FF).
    static inline uint32_t ComputeNumMips(uint32_t width, uint32_t height)
    {
        uint32_t HighBit;
        _BitScanReverse((unsigned long*)&HighBit, width | height);
        return HighBit + 1;
    }

    D3D12_RESOURCE_FLAGS CombineResourceFlags() const
    {
        D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE;

        if (Flags == D3D12_RESOURCE_FLAG_NONE && m_FragmentCount == 1)
            Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        return D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | Flags;
    }
protected:
    Color m_ClearColor;
    D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_RTVHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_UAVHandle[12];
    uint32_t m_NumMipMaps; // number of texture sublevels
    uint32_t m_FragmentCount;
    uint32_t m_SampleCount;
};